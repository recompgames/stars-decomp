/* WIN87EM.__fpMath (ordinal 1, task stars-0z4.8). The call is register based and BX (not AX)
 * selects the operation. The C runtime in segment 34 uses BX = 0 (init, with CF checked
 * afterwards), 2 (de-init), 3 (install the exception handler, DX:AX = 0110:1db4) and 0x0B
 * (is the 8087 installed; AX nonzero means yes). docs/inventory.md lists the call sites.
 * Semantics follow Wine dlls/win87em.dll16 (WIN87_Init, WIN87_SetCtrlWord,
 * WIN87_ClearCtrlWord). Plain C: the FPU is passed in and out through W87Regs, the harness
 * adapter (harness/toolhelp_win87em_thunks.c) moves it to and from the emulated x87. */
#ifndef STARS_RUNTIME_WIN87EM_H
#define STARS_RUNTIME_WIN87EM_H

#include <stdint.h>

#include "runtime/include/win16.h"

typedef struct {
  uint16_t ax, bx, dx; /* in: registers at the call; out: results */
  uint16_t fpcw, fpsw; /* x87 control and status word, in and out */
  int cf;              /* carry flag, in and out */
} W87Regs;

/* Returns 0 when BX is a supported operation, -1 for any other BX (nothing is changed;
 * the adapter aborts in strict mode and returns Wine's AX = DX = 0xFFFF otherwise). */
int win87em_fpmath(W87Regs *r);

/* State kept in the arena (rt_alloc): Wine's CtrlWord_1 (0x1332 after init, 0 before) and
 * the handler far pointer recorded by BX = 3 (0 before). */
uint16_t win87em_ctrl_word_1(void);
FARPTR win87em_handler(void);
void win87em_shutdown(void); /* frees the state block; call while the arena is alive */

#endif /* STARS_RUNTIME_WIN87EM_H */
