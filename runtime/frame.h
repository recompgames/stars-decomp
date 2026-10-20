/* Decompiled-function support shared by native and wasm builds (stars-dwf.3).
 *
 * DECOMP(seg, 0xOFF, name, CONV) precedes each decompiled function in the src directory (C files).
 * It expands to nothing; tools/gen_registry.py reads it. seg is the NE segment
 * number (1-35). CONV is one of the DtConv names below.
 *
 * Frame mirroring: a decompiled function whose locals have their address taken
 * reproduces the original stack frame, so a pointer to a local handed to an API
 * is bit-identical to the original's. FRAME_ENTER(n) mirrors "ENTER n,0" (PUSH
 * BP; MOV BP,SP; SUB SP,n) and FRAME_LEAVE mirrors "LEAVE" (MOV SP,BP; POP BP)
 * on the emulated stack (rt_ss:rt_sp, frame pointer rt_bp). BP_LOCAL(type, off)
 * is the lvalue of the local at BP+off (off is negative for locals). Original
 * locals can sit at odd BP offsets, so the access is unaligned-safe (a packed
 * wrapper struct gives the lvalue alignment 1); no alignment is required.
 *
 * CALL_ORIG(seg, off, conv, nargs, ...) calls code that is not decompiled yet,
 * passing nargs 16-bit words (PASCAL: push order; CDECL: declaration order; a
 * 32-bit argument is two words, see harness/harness.h). Native builds run it in
 * the Unicorn harness. In wasm it aborts with "not decompiled: seg:off".
 */
#ifndef STARS_RUNTIME_FRAME_H
#define STARS_RUNTIME_FRAME_H

#include <stdint.h>

#include "runtime/mem.h"

typedef enum DtConv { FAR_PASCAL, FAR_CDECL, NEAR_PASCAL, NEAR_CDECL, REGISTER } DtConv;

#define DECOMP(seg, off, name, conv)

extern uint16_t rt_ss, rt_sp, rt_bp; /* emulated stack of the running C code */

void rt_frame_enter(uint16_t nbytes);
void rt_frame_leave(void);
void rt_not_decompiled(unsigned seg, unsigned off) __attribute__((noreturn));
void rt_wasm_missing(const char *what) __attribute__((noreturn));

#define FRAME_ENTER(n) rt_frame_enter((uint16_t)(n))
#define FRAME_LEAVE() rt_frame_leave()
#define BP_LOCAL(type, off) \
  (((struct { type v; } __attribute__((packed)) *)LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + (off)))))->v)

/* rt_call_far(fn, conv, args, nargs): call the far pointer fn (selector:offset,
 * a selector below 0x100 is an NE segment number) with nargs 16-bit words. In
 * native builds it lives in harness/difftest.c (ORIGINAL mode always emulates;
 * DECOMP mode uses the registry's C function when there is one). The wasm build
 * has no emulator and no registry lookup yet (stars-bfj), so there it aborts
 * with a clear message instead of failing to link. FAR conventions only; a NEAR
 * convention aborts. */
#ifdef __EMSCRIPTEN__
#define CALL_ORIG(seg, off, conv, nargs, ...) (rt_not_decompiled((seg), (off)), 0u)
static inline uint32_t rt_call_far(uint32_t fn, int conv, const uint16_t *args, int nargs) {
  (void)fn; (void)conv; (void)args; (void)nargs;
  rt_wasm_missing("rt_call_far");
}
#else
uint32_t rt_call_orig(unsigned seg, unsigned off, int conv, int nargs, ...);
#define CALL_ORIG(seg, off, conv, nargs, ...) rt_call_orig((seg), (off), (conv), (nargs), ##__VA_ARGS__)
uint32_t rt_call_far(uint32_t fn, int conv, const uint16_t *args, int nargs);
#endif

#endif /* STARS_RUNTIME_FRAME_H */
