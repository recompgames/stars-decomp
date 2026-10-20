# Decompilation and runtime conventions

Rules (a) to (j2) apply to every file in `src/` (stars-bfj.1). Existing rules change only with the
owner's approval; new rules may be added. The "Floating point" section after them was written by
stars-dwf.4 and is rule (g). `src/` and `tests/scenarios/` are public; `docs/functions.csv` is
local-only (docs/PLAN.md decision 8). Readable code (pass 2) follows docs/PLAN.md §3 decisions 3
and 4 and the metrics in §5. `python3 tools/lint_src.py` (also run by `make test`) enforces what a
tool can check; the diff tests enforce the rest. The original binary is the oracle, never Ghidra's
pseudo-C.

## Decompilation rules

**(a) Types.** Only `int8_t`, `int16_t`, `int32_t` and the `uint` forms, `FARPTR` (`uint32_t`
selector:offset, `runtime/include/win16.h`) and `NEARPTR` (`uint16_t` offset into DGROUP,
`src/stars.h`), plus `bool`. Banned in `src/`: plain `int`, `short`, `long`, `unsigned`, `signed`;
`char` (use `int8_t`/`uint8_t`); `uintptr_t`/`intptr_t`/`ptrdiff_t`; host pointers stored in arena
memory (a host pointer from `LIN()` lives only inside one expression or one function and is never
written to game memory or kept in a game struct); `malloc`/`calloc`/`realloc`/`free`; mutable C
globals (game globals live in DGROUP and are reached through the macros in `src/globals.h`,
which `tools/gen_globals.py` generates from Ghidra's labels; constant tables may be `static const`;
`static` inside a function body is a mutable global too and is rejected).
`tools/gen_registry.py` also rejects these types in the signature of a DECOMP function.

**(b) Truncation.** Every value the original kept in a 16-bit register is cast to `int16_t` or
`uint16_t` before compares, shifts, divisions and stores, and again after arithmetic that can carry
out of 16 bits (C promotes to a 32-bit `int`, the CPU does not). Signed and unsigned compares mirror
the original's jump: JL/JG/JLE/JGE are `int16_t` compares, JB/JA/JBE/JAE are `uint16_t` compares.
Avoid signed overflow in C (undefined behaviour): do the arithmetic on `uint16_t`/`uint32_t` and
cast the result. Right shifts follow the instruction: SAR is an arithmetic shift of an `int16_t`
(or `int32_t`), SHR a logical shift of a `uint16_t` (or `uint32_t`); the wrong signedness changes
the high bits of negative values. IDIV and DIV raise a divide error (#DE) when the divisor is 0 or
the quotient does not fit (for example IDIV of -32768 by -1, or a 32/16 DIV whose quotient exceeds
16 bits). The original traps there, so the C must not silently give a value (after promotion to
`int`, C returns 32768 for `-32768 / -1`): check the divisor and the range explicitly and stop with
an abort or the recorded-error hook.

**(c) Memory access.** `LIN(fp)` and `LINN(off)` give a transient host pointer (it dies with the
statement; never store it, never use it across a call that can move memory). Game structs come from
`src/stars.h`: packed (`PACKED`), with `STATIC_SIZE` on `sizeof` and `STATIC_OFFSET` on `offsetof`
of every field, so a layout mistake fails the build. Stepping a huge pointer (the original's
`__AHINCR` code, 8 per 64 KB; `__AHSHIFT` = 3) uses `HUGE_ADD(fp, n)`. Selector of NE segment n is
`SEG_SEL(n)` (n << 3), DGROUP is `MEM_SEL_DGROUP` (0x0118).

**(d) Frames.** Each decompiled function mirrors SP at every call site: it reproduces its full
original frame, INCLUDING the registers it pushes after `ENTER`, so the stack addresses its callees
(and the callbacks they invoke) see are those of the original. `FRAME_ENTER(n + 2 * r)` stands for
`ENTER n,0` followed by `r` register pushes (PUSH SI, PUSH DI, ...), `FRAME_LEAVE()` before each
return, `BP_LOCAL(type, -x)` for the local at BP-x (`runtime/frame.h`). Every non-leaf function whose
callees may expose a stack address mirrors its frame, not only functions whose own locals have their
address taken; a leaf function that never exposes a stack address may use ordinary C locals. Original
locals can sit at odd BP offsets (a word at BP-3 is common), so `BP_LOCAL` uses unaligned access and
any `type` may be used at any offset. A pointer to a mirrored local is bit-identical to the original's
only if every frame between the entry and that function is mirrored too: an unmirrored caller, a
caller that pushes SI/DI after `ENTER` without accounting for it, or a nested C call that skips the
frame would shift SP and the pointer. A decompiled callee that mirrors a frame is therefore never
reached by a bare C call (which pushes no arguments and no return address): it is called through
`rt_call_far` (or a helper that pushes the arguments and a return address). Exported procedures start
with `ENTER n,0`.

**(e) Calls.** Decompiled to decompiled: by C name when the callee mirrors no frame (a leaf using
ordinary C locals); when the callee mirrors a frame (rule d) it is called through `rt_call_far`
(arguments pushed in the convention's order, with a return address), never as a bare C call. To code
not yet decompiled: `CALL_ORIG(seg, 0xOFF, conv, nargs, ...)` (the lint rejects it once the target is
decompiled). `CALL_ORIG` is far-only: it always makes a far call, and a NEAR convention aborts
(`rt_call_orig` in `harness/difftest.c`). To Win16 APIs: by name (`runtime/*.h`,
`runtime/include/win16.h`); native builds log each by-name call to the harness API log through
`harness/byname.h` (included by `src/stars.h`), exactly as the
import thunk does, so the diff sees it. A new API needs one wrapper there; never call an import
thunk from `src/`. Through a far pointer or callback: `rt_call_far`. The Win16 API call
order is part of the diff.

**(f) Annotations and naming.** A `DECOMP(seg, 0xOFF, name, CONV)` line directly before each
function (`CONV` is `FAR_PASCAL`, `FAR_CDECL`, `NEAR_PASCAL`, `NEAR_CDECL` or `REGISTER`);
`tools/gen_registry.py` builds the diff-test registry from it. The function is external (not
`static`, not `inline`). One file per original segment, `src/segNN.c` with a two-digit NN. A
function takes its export name if it has one, otherwise a meaningful Ghidra name, otherwise a
readable name derived from what the function does (owner decision 2026-10-20, PLAN.md phase 5), otherwise
`fn_NN_OOOO` (segment, hex offset). The C name in `src/` is the canonical name. For a segment that
has been renamed in phase 5, the `name` column of `docs/functions.csv` holds the same name; for a
segment not yet renamed it keeps Ghidra's name. The `DECOMP` line carries the C name only. The
comment above the function records the address, the Ghidra name (for example `FUN_1008_0000`) and
the old `fn_NN_OOOO` alias, so the mapping stays traceable. A comment above it records the instructions, the return
registers and anything Ghidra's output hides (for example DX = CS in the return value).

**(g) Floating point.** The decision recorded in the "Floating point" section below: SoftFloat
`Fp80` for x87 arithmetic, never `double` arithmetic or `long double`.

**(h) Ghidra idioms.**
- `CONCAT22(seg, off)` becomes `MK_FP(seg, off)`.
- `SUB42(x, 0)`, `(short)` and `(ushort)` casts become explicit `int16_t`/`uint16_t` casts.
- `in_DX`, `unaff_SI` and similar become explicit parameters or return values, chosen from the
  calling convention actually used (read the disassembly: a pseudo-C `int` return that ends with
  `MOV DX,...` is a 32-bit return).
- Reads of a segment register become selector constants (`SEG_SEL(n)`, `MEM_SEL_DGROUP`).
- Ghidra type map (Ghidra's `int`/`uint` are 16-bit): `int` -> `int16_t`, `uint` -> `uint16_t`,
  `long`/`ulong` -> `int32_t`/`uint32_t`, `undefinedN` -> `uintN_t` (`undefined1` -> `uint8_t`,
  `undefined2` -> `uint16_t`, `undefined4` -> `uint32_t`), `far char *` -> `FARPTR`, `CARRY2(a, b)`
  and similar helpers -> explicit 32-bit arithmetic, `code *` -> `rt_call_far`, `_DAT_`/`DAT_`
  addresses -> `g_` macros from `src/globals.h`.
- Pointer arithmetic on far parameters becomes struct access through `LIN()` on a `src/stars.h`
  struct, or `HUGE_ADD` for huge pointers.

**(i) Statuses** in `docs/functions.csv` go `todo`, then `decompiled` (a DECOMP line exists), then
`tested` (its DIFF scenarios pass). `unused` means no callers, not an entry point and not
referenced from data; the evidence is written in `notes`. `python3 tools/progress.py [--seg N]`
prints the counts and percentages.

Globals: `src/globals.h` is generated, never hand-edited (`ghidra/ExportGlobals.java` writes
`re/globals.csv`, `python3 tools/gen_globals.py` turns it into the header; a 16/32-bit global at a
misaligned offset is emitted through an align-1 typedef so the access is not undefined behaviour). A
`DAT_1110_xxxx` address that has no `g_` macro (16 of the 1003 referenced addresses today, for
example 0x76, 0x160, 0x96a) must first be defined as a data label in Ghidra with its type; then
rerun `ExportGlobals` and `gen_globals.py`. Do not write `LINN(0x....)` by hand to get around it.

**(j) Tests.** Every decompiled function has at least one `DIFF(seg, 0xOFF, conv, args...)` in
`tests/scenarios/test_segNN.c` (the lint requires it for `tested`). Scalar-only pure functions also
get `DIFF_FUZZ`. Coverage of the original function is at least 90 percent, or `notes` in
`docs/functions.csv` justify the gap. Before trusting a scenario, change one constant in the C
function and see the DIFF fail, then restore the file and check it with `cmp`.

**(j2) Test doubles.** A test double (a "stub": the first bytes of a callee in the code segment image
are replaced so that both the ORIGINAL run and the C version's `CALL_ORIG` execute it) is allowed
only for a callee whose real behaviour is not under test (not decompiled yet and too large or too
state-hungry to run in isolation) and whose side effects on the state the diff compares are
recorded, or are absent. It must never replace the function under test. The callee must be CDECL
(the caller pops the arguments) or take no argument, and the double must be declared with the exact
argument count of the original. Every double used by a function is listed in that function's
`notes` in `docs/functions.csv`, with its blind spots. Make the double a logged thunk (as in
`tests/scenarios/test_seg03.c`: `stub_install` jumps to an import registered with
`harness_add_import`), so that the call, its argument words and its order against the Win16 API
calls and the other doubles are entries of the API log that DIFF compares. A double that records
only into its own memory block compares calls against each other and does not check their order
against API calls or writes to DGROUP and far memory. Even a logged thunk records a pointer argument
as the pointer only, and does not order the call against plain memory writes: where the pointed-to
content or such a write matters, register a watch that records a hash of that memory at call time,
and state in the `notes` which memory is watched and which is not. Before trusting a double, move
the stubbed call across an API call and across the write it should follow (or precede) in the C
version and see the DIFF fail, then restore the file and check it with `cmp`.

### Worked example: `2:0x0000` (`fn_02_0000`)

`2:0x0000` is one of four same-shape functions in segment 2 (`2:0x0000`, `0x50a2`, `0x50b0`,
`0x50ca`); it was chosen because it exercises an argument and a DX:AX return. The literal smallest
leaf is a bare RETF at `4:0x4db8`, which exercises neither. Original (15 bytes):
`PUSH BP; MOV BP,SP; IMUL AX,[BP+6],0x8F; ADD AX,0x5C3; MOV DX,CS; LEAVE; RETF`, FAR CDECL, the
index is the one `int16_t` argument at `[BP+6]`, and the result is the far pointer `cs:AX`.

```c
DECOMP(2, 0x0000, fn_02_0000, FAR_CDECL)
uint32_t fn_02_0000(int16_t index)
{
  uint16_t off = (uint16_t)((uint16_t)index * 0x8f + 0x5c3);
  return MK_FP(SEG_SEL(2), off);
}
```

Ghidra shows `int FUN_1008_0000(int param_1) { return param_1 * 0x8f + 0x5c3; }`: it loses DX = CS
(rule h) and the 16-bit wrap (rule b). `tests/scenarios/test_seg02.c` has `DIFF` cases for the
boundary indices and a `DIFF_FUZZ` run. Mutations that make it fail: `0x8f` to `0x90` (AX differs)
and `SEG_SEL(2)` to `SEG_SEL(3)` (DX differs).

## Floating point

**Decision: use SoftFloat extF80 (Berkeley SoftFloat, "3e plus master changes", vendored in
`third_party/softfloat/`), not C `double`, for every x87 basic operation in
decompiled code.** The helpers are in `runtime/fp80.h` / `runtime/fp80.c`
(`fp80_from_f64`, `fp80_add/sub/mul/div`, `fp80_sqrt`, `fp80_to_f64`, `fp80_to_i16/i32`,
`fp80_to_i32_ftol`, `fp80_set_cw`). A decompiled `FLD m64; FADD m64; FSTP m64` becomes
`fp80_to_f64(fp80_add(fp80_from_f64(a), fp80_from_f64(b)))`, and values that
stay on the x87 stack between instructions stay `Fp80`.

### Control word: owner decision 0x1330 (stars-ckz)

- **Full runs use `FPU_CW_DEFAULT` = 0x1330** (`runtime/mem.h`, next to
  `WINFLAGS_DEFAULT`; owner decision 2026-10-15): PC = 11b (64-bit significand),
  RC = 00b (nearest even), IM/DM/ZM/OM unmasked, UM/PM masked. It is the word Wine's
  WIN87EM init programs. The harness loads it at `harness_load` (Unicorn boots with
  FPCW = 0, 24-bit precision, which no real x87 has) and `runtime/win87em.c`
  `__fpMath` BX = 0 and 2 load the same constant, so the two cannot drift. It is still
  an inference about the real Win3.1 word (WIN87EM is not in the repo), not a
  measurement. `test_default_control_word` and `test_default_control_word_registered`
  in `harness/tests/test_fp.c` pin it at WinMain without and with the WIN87EM
  registration.
- **The masked model 0x037F is no longer used for full runs.** It survives only in
  tests that compare NORMAL results (the equivalence tests below), whose values do not
  depend on the mask bits.
- **What the start-up path does (verified by `test_control_word_sentinel`).** The C
  runtime path from `34:0x001A` to WinMain (`3:0x0000`) DOES run FNSTCW `[bp-6]`,
  then FNINIT at `34:0x265a`, then compares, then FNCLEX and FLDCW `[bp-6]` at
  `34:0x26b1`. It is a save / FNINIT / restore probe and is net-neutral: a sentinel
  word of 0x1332 set before the call is still 0x1332 at WinMain (the test also
  asserts the FNINIT and the FLDCW are executed, and that the word is 0x037F between
  them). The earlier premise "no FLDCW/FNINIT on the path" was wrong.
- **`WIN87EM.__fpMath` codes are in BX, not AX**: BX = 0 init, BX = 3 installs an
  exception handler (`0110:1db4` = `34:0x1db4`), BX = 0xB; BX = 2 at `34:0x024a` is
  outside this path. No code calls BX = 4, and the game never sets PC below 64-bit.
  (BX codes: from the 2026-10-15 review, not re-derived by the test.)
- **The real word is most likely PC = 11b with IM/DM/ZM/OM unmasked: 0x1330, or
  0x1332 (the same with DM masked)** (Wine's win87em init gives 0x1330; the C runtime
  uses the 0x1332 family itself). Handler: `34:0x1db4`. This is an inference, not a
  measurement: WIN87EM is not in the repo.
- **What is and is NOT modelled with exceptions unmasked** (`test_exceptional_paths`).
  Modelled in `fp80`: every basic operation, FLD m64 (`fp80_from_f64`), FSTP m64
  (`fp80_to_f64`) and FISTP raise the x87 flags IE, DE, ZE, OE, UE, PE into a sticky
  word (`fp80_exceptions`, `fp80_clear_exceptions`; DE for a denormal operand or a
  denormal double load). A raised flag whose mask bit is 0 in the control word calls the
  handler set by `fp80_set_trap_handler` with the unmasked bits: that is the "trap =
  stop with a recorded error" hook. Modelled in the harness: opt-in
  `harness_set_fp_trap_check(true)` clears the flags on entry to a top-level
  `harness_call` and, if an unmasked flag is set on return, ends the call with
  `harness_last_error() != 0`, a stderr line, and `harness_fp_trap_pending()` giving
  the bits. NOT modelled:
  - **No vectoring into the game's handler at 34:0x1db4.** A trap is a stop, never
    handler code running; nothing is rolled back (destination, x87 stack, status word).
    `fp80` still returns the masked-response IEEE value, which the caller must treat as
    invalid once the handler was called; a decompiled caller that ignores the handler
    gets a silent IEEE result.
  - **The Unicorn x87 does not trap and reports few flags.** Measured at 0x1330: it
    computes the IEEE result without raising an exception, and sets a status flag only
    for division by zero (also for 0/0, where a real x87 sets IE). It sets nothing for
    inf-inf, overflow at FSTP m64 or a denormal operand, so the harness check cannot
    see those traps (the test pins this); only `fp80` reports them. The check is
    after the fact, per top-level call, not per instruction.
  - UE and PE are masked in 0x1330, so underflow and inexact never trap here. Other
    unmasked words, FPREM / transcendental / FSQRT exceptions, FXAM, compare (FCOM)
    invalid on NaN and the 32-bit and 16-bit load/store forms are not modelled
    (helpers pending: stars-4vx).
- The C runtime only changes the word temporarily, inside its own helpers, in
  segment 34 (Ghidra counts 12 FLDCW and 7 FSTCW there; the issue text said 11 and
  6):
  - `34:0x0de6`: FNSTCW, set RC bits to 11b (truncate), FLDCW, FISTP m64, FLDCW
    (restore). This is the double-to-long cast (`ftol`).
  - `34:0x2068`, `34:0x20aa`: FNSTCW, then low byte |= 0x38 and high byte = 0x13
    (PC = 11b, IC = 1, OM/UM/PM masked), FLDCW, FXAM classification for the
    transcendental error paths, then restore at `34:0x1cc3`.
  - the other sites are in undecoded ranges of segment 34 (`RANGE_34_*` rows of
    `docs/functions.csv`) and were not read individually. The five decoded sites
    (`34:0x0e07`, `0x0e0f`, `0x1cc3`, `0x2078`, `0x20ba`) never select a precision
    below 64-bit.

### Reason

With PC = 64-bit, a basic operation is rounded to a 64-bit significand and then
rounded again to 53 bits by FSTP m64. Plain C `double` rounds once, so it differs
whenever the 64-bit result lies near a 53-bit halfway point. SoftFloat extF80
reproduces both roundings. Measured over 100000 seeded random operand pairs per
operation (xorshift64*, seed `0x5EEDF00DCAFE1234`), Unicorn x87 versus:

| operation | fp80 mismatches | plain C double mismatches (non-NaN) |
|-----------|-----------------|--------------------------------------|
| FADD + FSTP m64 | 0 | 17 |
| FSUB + FSTP m64 | 0 | 15 |
| FMUL + FSTP m64 | 0 | 28 |
| FDIV + FSTP m64 | 0 | 23 |
| FISTP m16, FISTP m32 | 0 | (not compared) |

Example counterexample, FADD, operands `0x40e7bfa39c48756d` and
`0x3f864a1220e0038b`: x87 gives `0x40e7bfa3f570bdf0`, plain double
`0x40e7bfa3f570bdf1`. `test_fp.c` asserts that the plain-double count is above 0,
so the test is known to be able to tell the two apart. Operands are random
doubles (exponents mostly within 2^+-40, 9 percent across the whole finite range,
a few +-0 and +-inf; no NaNs; the second operand is within 60 binary orders of the
first half of the time). Overflow, underflow to subnormal, 0/0 and inf-inf NaN
bits (negative quiet NaN, "real indefinite") are covered and identical.

The helpers also honour the rounding control and the precision control of the
control word given to `fp80_set_cw`: `test_fp.c` checks 24-bit, 53-bit and 64-bit
precision with all four rounding modes (10000 pairs each, 0 mismatches), including
FISTP m16/m32 (ties and out-of-range values give the integer indefinite 0x8000 /
0x80000000).

### What the equivalence tests do not verify

- **NaN operands** are not verified for the arithmetic tests: x87 and SoftFloat can
  differ in SNaN quieting and in which of two NaN operands is returned. The committed
  arithmetic tests (FADD/FSUB/FMUL/FDIV, FISTP m16/m32) generate no NaN and no
  denormal operands, although denormal OUTPUTS occur (underflow). The only NaN
  operands in the committed tests are in the ftol test (result 0 for any NaN) and the
  NaN produced by 0/0 and inf-inf (compared as results).
- The equivalence tests run with all exceptions masked (0x037F and variants), so the
  trap behaviour is NOT compared against the Unicorn FPU, which does not trap
  (see "What is and is NOT modelled" above); `test_exceptional_paths` checks fp80's
  flags and trap calls against intended x87 semantics and pins the Unicorn gap.

### Rules for decompiled code

1. Basic operations (FADD, FSUB, FMUL, FDIV and the reversed forms) on values that
   pass through the x87 stack: use `Fp80`. Never `double` arithmetic, and never
   `long double` (64-bit on arm64 macOS and in WebAssembly).
2. Loads (FLD m32/m64, FILD) convert exactly to `Fp80`; stores (FSTP m32/m64)
   round with the current rounding control (FSTP m32 double-rounds like m64: 64-bit
   significand first, then 24); FISTP rounds by RC (nearest even by default) and gives
   the indefinite value on overflow. **Helpers pending: stars-4vx.** Only
   `fp80_from_f64`, `fp80_from_i32`, `fp80_to_f64`, `fp80_to_i16/i32` and
   `fp80_to_i32_ftol` exist today; FLD m32, FSTP m32, FILD m16/m64 have none yet.
3. Leave the control word at the default unless the original code sets it; mirror its
   FLDCW with `fp80_set_cw`. Full runs use `FPU_CW_DEFAULT` (0x1330): call
   `fp80_set_cw(FPU_CW_DEFAULT)` and install a trap handler that stops the run.
   `FP80_CW_DEFAULT` (0x037F, FNINIT, all masked) is only for the normal-value tests.
4. Compare and test instructions are exact on both representations.
5. FSQRT is covered: `fp80_sqrt` is proven against the Unicorn x87 by `test_sqrt` in
   `harness/tests/test_fp.c` (random doubles, perfect squares and their neighbours, negative
   operands: 0 mismatches under 0x037F, 0x137F and 0x027F). It was added for 33:0x01a0, whose
   call to the C runtime's sqrt (34:0x0d68) is written as a helper that also reproduces the
   runtime's stores into DGROUP and its control word change (see `crt_sqrt_34_0d68` in
   `src/crt34.h`, used by every segment that calls 34:0x0d68; the table above lists them).
   The proof covers zero, positive normal and negative normal operands, the reachable domain;
   NaN, infinity and denormal operands are outside it.
   FYL2X and F2XM1 are `fp80_fyl2x` and `fp80_f2xm1` (stars-nf6), the two steps of the C
   runtime's pow (34:0x0d84, `crt_pow_34_0d84` in `src/crt34.h`; no host `libm` is used). They
   evaluate the double expressions `log(x) / log(2.0) * y` and `pow(2.0, x) - 1.0` (the form
   Unicorn's QEMU 5.0 x87 uses), with a correctly rounded double `log` and `2^x` from 128-bit
   integer arithmetic and the division, multiplication and subtraction as double operations (see
   `runtime/fp80.h`). This is not the correctly rounded `2^x - 1`: `fp80_f2xm1` returns
   RN(RN(2^x) - 1). Unicorn's own FYL2X and F2XM1 call the HOST libm, whose `log` and `exp2` are
   not correctly rounded (an earlier measurement, not recorded in the repository, found the result
   1 ulp off the helpers for a small fraction of operands; see stars-5yb), so the oracle now uses
   the same helpers
   (owner decision 2026-10-20): `harness_hook_x87_transc` in `harness/harness.c` runs them for the
   original's FYL2X and F2XM1 instructions (`harness_load` hooks segment 34 at 0x1367, 0x137a and
   0x140d; operands outside the helpers' domain are left to Unicorn). The hook also covers
   segment 34's internal ln path (the FYL2X at site 0x1367) and exp path (which runs the F2XM1 at
   site 0x140d), neither of which is called from outside segment 34. The oracle and the C path
   therefore agree by construction: `test_fyl2x_equiv` and `test_f2xm1_equiv` in
   `harness/tests/test_fp.c` check that the hooked oracle and the C path are bit-exact (110000
   operands each), and `t_pow_34_0d84_native` in `tests/scenarios/test_seg21.c` checks the whole pow
   bit-exact with segment 21's operands. The VALUES of FYL2X and F2XM1 are verified only against
   the reference vectors (`test_fp_vectors` in `test_fp.c`, 70-digit decimal
   arithmetic), not against a real x87 (64-bit significand, microcode); follow-up stars-5yb.
   The other transcendental instructions (FPTAN, FPATAN, FSCALE, FRNDINT,
   FPREM) are not covered by this proof. Their results on a real x87 and on
   SoftFloat can differ in the last bit. Each one needs its own equivalence test
   when a decompiled function uses it. They occur only in segment 34 (see the
   table in `docs/inventory.md`), plus the raw-byte matches that are data.

### C runtime math functions called from the game

Functions in segment 34 that do floating point and have callers outside segment 34:
three, `ftol` (`fp=1` in `docs/functions.csv`), `sqrt` (34:0x0d68 is a 6-byte entry stub, `fp=0`,
that jumps to 34:0x1c34, which runs FSQRT) and `pow` (34:0x0d84, a 6-byte stub that jumps to
34:0x1c04, which runs FYL2X, FRNDINT, F2XM1 and FSCALE).

| address | name | callers outside seg 34 | meaning |
|---------|------|------------------------|---------|
| `34:0x0de6` | `FUN_1108_0de6` | 42 call sites in 15 segments (5, 7, 8, 9, 10, 11, 16, 18, 21, 22, 23, 28, 30, 32, 33) | double to 32-bit long conversion (`ftol`): FISTP m64 under RC = truncate, result DX:AX. Use `fp80_to_i32_ftol` as the reference for the game's behaviour. A plain C cast `(long)d` is exact only for \|d\| < 2^31: the original wraps out-of-range values (2^32 + 5 gives 5) and gives 0 for \|d\| >= 2^63 or NaN (integer indefinite, low 32 bits). The old control word is restored. Proven by `test_fp.c` against FISTP m64 (opcode DF /7) in Unicorn under RC = 11b, 100000 seeded operands including out-of-range, 2^32 + 5, 2^63 and NaN: 0 mismatches. |
| `34:0x0d68` | `FUN_1108_0d68` | 27 call sites in 8 segments (5 x1, 7 x6, 8 x2, 9 x3, 11 x8, 18 x5, 22 x1, 33 x1) | square root (`sqrt`): FSQRT of the operand in ST(0), run under CW = (low byte \| 0x38) \| 0x1300 (the old word is restored). DGROUP stores: the operand as a double at DS:0x1C6A only when DS:0x1E1E is 0, the result as a double at DS:0x18FC, and the byte DS:0x1C9A = 1. Use `crt_sqrt_34_0d68` in `src/crt34.h` as the reference (it uses `fp80_sqrt` and mirrors the CW change and the stores). A negative operand takes the domain-error path (matherr hook, message), which is not modelled: the helper aborts. Proven on the reachable domain only: `fp80_sqrt` against FSQRT in Unicorn by `test_sqrt` in `test_fp.c` (zero, positive normal and negative normal operands, 0 mismatches), the stores by the 33:0x01a0 diff, the CW restore by `t_crt_sqrt_cw` in `test_seg33.c`. |
| `34:0x0d84` | `FUN_1108_0d84` | 3 call sites in 2 segments (21:0x29da at `src/seg21.c` line 368, 21:0x123e at line 1919, 7:0x0262) | power (`pow`): ST(1) is the base, ST(0) the exponent, the result replaces both. t = exponent * log2(base) (FYL2X), n = FRNDINT(t), f = t - n, r = F2XM1(\|f\|) + 1 (1 / r when f < 0), result = r * 2^n (FSCALE); a zero exponent gives 1.0. Run under CW = (low byte \| 0x38) \| 0x1300 (restored); stores the base and exponent as doubles at DS:0x1C6A / 0x1C72 when DS:0x1E1E is 0, the result at DS:0x18FC and the byte DS:0x1C9A = 1. Use `crt_pow_34_0d84` in `src/crt34.h` as the reference (it uses `fp80_fyl2x` and `fp80_f2xm1`, no host libm). A base that is not positive and normal, a non-finite or denormal exponent and \|t\| >= 2^14 take the domain/overflow paths (matherr hook), which are not modelled: the helper aborts. The oracle runs the same helpers for FYL2X and F2XM1 (`harness_hook_x87_transc`), so `t_pow_34_0d84_native` (`test_seg21.c`) is bit-exact against the original's sequence; the helpers' values are checked only by the reference vectors in `test_fp.c`, not against a real x87 (see Floating point rule 5, follow-up stars-5yb). |

Every other floating point function in segment 34 is called only from inside segment 34
(the transcendental implementations and their helpers). The game segments contain
no calls to them, so nothing outside `ftol`, `sqrt` and `pow` needs a replacement. None of the three uses the host `libm`: `pow` is `fp80_fyl2x` and `fp80_f2xm1`, so there is no `libm` exception.

### Build and test

- `third_party/softfloat/sources.mk` lists the SoftFloat sources built (the 3e
  FAST_INT64 object list); the Makefile builds them into `build/libsoftfloat.a`
  without `-Werror` and sanitizers. License: `third_party/softfloat/LICENSE`
  (BSD-3-Clause).
- `make test` runs `harness/tests/test_fp.c` (about 1 second).
