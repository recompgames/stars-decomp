/* Emulated frame pointer for decompiled functions (runtime/frame.h). */
#include "runtime/frame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint16_t rt_ss, rt_sp, rt_bp;

void rt_frame_enter(uint16_t nbytes) {
  rt_sp = (uint16_t)(rt_sp - 2);
  memcpy(LIN(MK_FP(rt_ss, rt_sp)), &rt_bp, 2);
  rt_bp = rt_sp;
  rt_sp = (uint16_t)(rt_sp - nbytes);
}

void rt_frame_leave(void) {
  rt_sp = rt_bp;
  memcpy(&rt_bp, LIN(MK_FP(rt_ss, rt_sp)), 2);
  rt_sp = (uint16_t)(rt_sp + 2);
}

void rt_not_decompiled(unsigned seg, unsigned off) {
  fprintf(stderr, "not decompiled: %u:%04x\n", seg, off);
  abort();
}

void rt_wasm_missing(const char *what) {
  fprintf(stderr, "%s is not available in the wasm build yet (needs the registry lookup, stars-bfj)\n", what);
  abort();
}
