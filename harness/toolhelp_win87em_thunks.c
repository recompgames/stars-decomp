/* HCall decoders for TOOLHELP.TimerCount and WIN87EM.__fpMath. Native builds only. */
#include "harness/toolhelp_win87em_thunks.h"

#include <stdio.h>

#include "harness/harness.h"
#include "runtime/toolhelp.h"
#include "runtime/win87em.h"

/* TimerCount(TIMERINFO FAR *): one far pointer, high word pushed first. */
static void t_TimerCount(HCall *c) {
  uint32_t fp = (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16);
  c->ret = TimerCount(fp);
}

/* __fpMath: BX selects the operation. The x87 control and status words go through the
 * emulated FPU; an unsupported BX aborts in strict mode, else AX = DX = 0xFFFF (Wine). */
static void t_fpMath(HCall *c) {
  Regs16 *g = c->regs;
  W87Regs r;
  r.ax = g->ax; r.bx = g->bx; r.dx = g->dx;
  r.fpcw = harness_fpcw();
  r.fpsw = harness_fpsw();
  r.cf = g->flags & 1;
  if (win87em_fpmath(&r) != 0) {
    if (harness_is_strict()) {
      fprintf(stderr, "harness: UNIMPLEMENTED WIN87EM.__fpMath BX=0x%04X (strict mode)\n", g->bx);
      harness_abort_run();
      return;
    }
    g->ax = 0xFFFF;
    g->dx = 0xFFFF;
    return;
  }
  g->ax = r.ax;
  g->dx = r.dx;
  g->flags = (uint16_t)((g->flags & ~1u) | (r.cf ? 1u : 0u));
  harness_set_fpcw(r.fpcw);
  harness_set_fpsw(r.fpsw);
}

void toolhelp_win87em_register(void) {
  harness_register("TOOLHELP", 80, t_TimerCount);
  harness_register("WIN87EM", 1, t_fpMath);
}
