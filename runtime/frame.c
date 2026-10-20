/* Emulated frame pointer for decompiled functions (runtime/frame.h). */
#include "runtime/frame.h"

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
