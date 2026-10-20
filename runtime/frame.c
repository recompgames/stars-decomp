/* Emulated frame pointer for decompiled functions (runtime/frame.h). */
#include "runtime/frame.h"

#include <string.h>

uint16_t rt_ss, rt_sp, rt_bp;
uint16_t rt_entry_si, rt_entry_di;

void rt_frame_enter(uint16_t nbytes) {
  rt_sp = (uint16_t)(rt_sp - 2);
  memcpy(LIN(MK_FP(rt_ss, rt_sp)), &rt_bp, 2);
  rt_bp = rt_sp;
  rt_sp = (uint16_t)(rt_sp - nbytes);
}

static void push_word(uint16_t w) {
  rt_sp = (uint16_t)(rt_sp - 2);
  memcpy(LIN(MK_FP(rt_ss, rt_sp)), &w, 2);
}

void rt_frame_enter_regs(uint16_t nlocals, unsigned nregs) {
  rt_frame_enter(nlocals);
  if (nregs >= 2) push_word(rt_entry_di); /* PUSH DI first (the original's order: 57 56) */
  if (nregs >= 1) push_word(rt_entry_si); /* then PUSH SI */
}

void rt_frame_enter_di(uint16_t nlocals) {
  rt_frame_enter(nlocals);
  push_word(rt_entry_di);
}

void rt_frame_leave(void) {
  rt_sp = rt_bp;
  memcpy(&rt_bp, LIN(MK_FP(rt_ss, rt_sp)), 2);
  rt_sp = (uint16_t)(rt_sp + 2);
}
