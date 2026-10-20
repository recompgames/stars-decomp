#include "runtime/win87em.h"

#include <string.h>

#include "runtime/mem.h"

#define STATE_MAGIC 0x38374D45u /* "87ME" */
#define W87_INSTALLED 1u
#define W87_INIT_WORD 0x1332u /* the control word the Windows emulator programs */

typedef struct {
  uint32_t magic;
  uint16_t ctrl1;    /* Wine CtrlWord_1 */
  uint16_t status2;  /* Wine StatusWord_2 (cleared by init) */
  uint32_t handler;
} State;

static uint32_t g_off;
static const uint8_t *g_base;

static State *st(void) {
  if (g_off && g_base != mem_arena()) g_off = 0;
  if (g_off && ((State *)(mem_arena() + g_off))->magic != STATE_MAGIC) g_off = 0;
  if (!g_off) {
    State *s = rt_alloc(sizeof *s);
    if (!s) return NULL;
    memset(s, 0, sizeof *s);
    s->magic = STATE_MAGIC;
    g_off = mem_off(s);
    g_base = mem_arena();
  }
  return (State *)(mem_arena() + g_off);
}

uint16_t win87em_ctrl_word_1(void) { State *s = st(); return s ? s->ctrl1 : 0; }
FARPTR win87em_handler(void) { State *s = st(); return s ? s->handler : 0; }

void win87em_shutdown(void) {
  if (g_off && g_base == mem_arena() && mem_arena()) rt_free(mem_arena() + g_off);
  g_off = 0;
  g_base = NULL;
}

/* WIN87_Init: FNINIT, set control word 0x1332 (stored whole, loaded without bits 0xC3,
 * i.e. FPU_CW_DEFAULT = 0x1330, runtime/mem.h), FCLEX, AX = 0. CF is cleared: the caller tests it for failure. */
static void w87_init(State *s, W87Regs *r) {
  s->ctrl1 = W87_INIT_WORD;
  r->fpcw = FPU_CW_DEFAULT; /* == W87_INIT_WORD & ~0x00C3 */
  r->fpsw = (uint16_t)(r->fpsw & ~0x80FFu); /* FCLEX */
  s->status2 = 0;
  r->ax = 0;
  r->cf = 0;
}

int win87em_fpmath(W87Regs *r) {
  State *s = st();
  if (!s) return -1;
  switch (r->bx) {
    case 0:  /* install emulator */
    case 2:  /* de-install emulator: Wine runs the same WIN87_Init */
      w87_init(s, r);
      return 0;
    case 3:  /* install exception handler at DX:AX; Wine ignores it, we record it */
      s->handler = ((uint32_t)r->dx << 16) | r->ax;
      return 0;
    case 0x0B: /* installed flag in DX:AX */
      r->dx = 0;
      r->ax = W87_INSTALLED;
      return 0;
    default:
      return -1;
  }
}
