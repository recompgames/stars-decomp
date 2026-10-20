/* Unicorn hybrid harness: loader, import thunks, API dispatcher, harness_call.
 *
 * SPIKE RESULTS (stars-dwf.2, Unicorn 2.1.4, Homebrew, measured with
 * harness/tests/test_harness.c test_spike_nested_and_int_hook):
 *
 *  1. UC_HOOK_INTR and INT 0xFE: in 16-bit protected mode with NO IDT loaded,
 *     UC_HOOK_INTR fires for "INT 0xFE" with intno == 0xFE. When the hook
 *     returns, execution resumes at the instruction after the INT (the
 *     reported IP is already past the INT, so for a thunk "CD FE CA nn nn"
 *     the hook sees IP = thunk offset + 2, the RETF). No code-range hook is
 *     needed.
 *  2. Nested emulation: calling uc_emu_start from inside that hook WORKS
 *     (returns UC_ERR_OK). The inner block ran, wrote its marker word, and
 *     stopped at its "until" address. The inner run overwrites the outer
 *     registers, so the outer context must be saved with uc_context_save
 *     before and put back with uc_context_restore after the inner run; with
 *     that, the outer run continued correctly after the INT and reached its
 *     own stop address.
 *
 * MECHANISM CHOSEN: nested uc_emu_start (harness_call is re-entrant). A
 * callback from an API handler into original code is a nested harness_call
 * that saves/restores the full register context and runs on the caller's
 * stack. No uc_emu_stop/continuation loop is used. INT 0xFE thunks are used
 * (no code-range hook).
 *
 * Other facts the code relies on: in protected mode CS is only loaded by a
 * far jump (so every run starts at a "JMP FAR CS:[var]" stub in the thunk
 * segment, and ends at a stop address inside the thunk segment so CS is a
 * valid thunk-segment selector between runs); in 16-bit mode uc_emu_start
 * takes LINEAR addresses.
 *
 * Thunk segment layout (selector MEM_SEL_THUNK, 64 KB):
 *   0x0000 + 8 * i   thunk i: CD FE, then CA nn nn (RETF argbytes) for PASCAL
 *                    and REGISTER imports that pop arguments, else CB (RETF),
 *                    padded with NOP to 8 bytes. Thunks are ordered by NE
 *                    module-table order, then ordinal ascending, one per
 *                    distinct imported (module, ordinal) including the
 *                    variable-style imports (equates), so the pointer values
 *                    stored in data are reproducible in the browser build.
 *   0xF000           call stub: loads DS, ES, SS, SP from the variables at
 *                    0xF040, loads the flags with PUSH/POPF, then JMP FAR CS:[0xF040]
 *   0xF040           variables: offset, selector, DS, ES, SS, SP, flags (words)
 *   0xF060           return stub: where harness_call's callee RETFs to
 *   0xF070           idle stub: where the boot run ends
 *   0xF080           reload stub: after a handler changes DS/ES, the interrupt
 *                    hook redirects here: MOV DS,CS:[0xF050]; MOV ES,CS:[0xF052];
 *                    JMP NEAR CS:[0xF054] (the thunk's RETF)
 *   0xF050           reload variables: DS, ES, return offset (words)
 * (uc_reg_write of DS/ES/SS in protected mode does not load the descriptor
 * cache, only the visible selector, so the call stub loads them with MOV.)
 */
#include "harness/harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "runtime/fp80.h"
#include "runtime/mem.h"
#include "runtime/ne.h"

#define THUNK_CALL_OFF 0xF000u
#define THUNK_VAR_OFF 0xF040u
#define THUNK_RET_OFF HARNESS_THUNK_RET_OFF
#define THUNK_IDLE_OFF 0xF070u
#define THUNK_RELOAD_VAR 0xF050u
#define THUNK_RELOAD_OFF 0xF080u
#define MAX_THUNKS (THUNK_CALL_OFF / 8)
#define MAX_HANDLERS 1024

/* ---- generated API table ---- */

typedef struct {
  const char *module;
  uint16_t ordinal;
  const char *name;
  int kind;
  uint16_t argbytes;
  int ret;
  uint16_t value; /* equates */
} ApiDef;

#define W16API(m, o, n, k, a, r) {#m, o, n, HK_##k, a, HR_##r, 0},
#define W16EQU(m, o, n, v) {#m, o, n, HK_EQU, 0, HR_NONE, v},
static const ApiDef g_defs[] = {
#include "runtime/win16_api.inc"
};
#undef W16API
#undef W16EQU

static const ApiDef *find_def(const char *module, uint16_t ordinal) {
  for (size_t i = 0; i < sizeof g_defs / sizeof g_defs[0]; i++) {
    if (g_defs[i].ordinal == ordinal && strcmp(g_defs[i].module, module) == 0) return &g_defs[i];
  }
  return NULL;
}

/* ---- state ---- */

typedef struct {
  char module[16];
  uint16_t ordinal;
  const char *name;
  int kind, ret;
  uint16_t argbytes;
  uint16_t value; /* equates */
  HApiFn fn;      /* from harness_add_import; registrations take precedence */
} Thunk;

typedef struct {
  char module[16];
  uint16_t ordinal;
  HApiFn fn;
} Handler;

static uc_engine *g_uc;
static uint32_t g_thunk_base; /* linear base of the thunk segment */
static Thunk g_thunks[MAX_THUNKS];
static size_t g_n_thunks;
static Handler g_handlers[MAX_HANDLERS];
static size_t g_n_handlers;
static HLogEntry *g_log;
static size_t g_n_log, g_cap_log;
static uint16_t g_winflags = WINFLAGS_DEFAULT;
static bool g_strict, g_quiet;
static bool g_fp_trap_check;
static uint16_t g_fp_trap_pending;
static uint16_t g_stack_top, g_stack_limit, g_call_sp;
static uc_hook g_trace_hook;
static bool g_trace_on;
static HTraceFn g_trace_fn;
static void *g_trace_ud;
static int g_depth;
static int g_last_err;
static unsigned g_api_limit, g_api_count;
static bool g_stopped_early;
static bool g_abort; /* sticky: harness_abort_run was called; cleared at depth 0 */
static uc_context *g_clean_ctx; /* saved right after boot */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }

static void fail(const char *msg) { fprintf(stderr, "harness: %s\n", msg); }

static void rd(int reg, void *v) { uc_reg_read(g_uc, reg, v); }
static void wr(int reg, uint16_t v) { uc_reg_write(g_uc, reg, &v); }

static void get_regs(Regs16 *r) {
  uint32_t fl = 0;
  rd(UC_X86_REG_AX, &r->ax); rd(UC_X86_REG_BX, &r->bx); rd(UC_X86_REG_CX, &r->cx);
  rd(UC_X86_REG_DX, &r->dx); rd(UC_X86_REG_SI, &r->si); rd(UC_X86_REG_DI, &r->di);
  rd(UC_X86_REG_BP, &r->bp); rd(UC_X86_REG_SP, &r->sp); rd(UC_X86_REG_DS, &r->ds);
  rd(UC_X86_REG_ES, &r->es); rd(UC_X86_REG_SS, &r->ss); rd(UC_X86_REG_CS, &r->cs);
  rd(UC_X86_REG_IP, &r->ip); rd(UC_X86_REG_EFLAGS, &fl);
  r->flags = (uint16_t)fl;
}

/* ---- thunks ---- */

static void write_thunk_bytes(size_t idx) {
  const Thunk *t = &g_thunks[idx];
  uint8_t *p = mem_arena() + g_thunk_base + idx * 8;
  memset(p, 0x90, 8);
  p[0] = 0xCD;
  p[1] = 0xFE;
  if ((t->kind == HK_PASCAL || t->kind == HK_REGISTER) && t->argbytes > 0) {
    p[2] = 0xCA;
    p[3] = (uint8_t)(t->argbytes & 0xFF);
    p[4] = (uint8_t)(t->argbytes >> 8);
  } else {
    p[2] = 0xCB;
  }
}

static bool add_thunk(const char *module, uint16_t ordinal, const char *name, int kind,
                      uint16_t argbytes, int ret, uint16_t value, HApiFn fn) {
  Thunk *t;
  if (g_n_thunks >= MAX_THUNKS) return false;
  t = &g_thunks[g_n_thunks];
  memset(t, 0, sizeof *t);
  snprintf(t->module, sizeof t->module, "%s", module);
  t->ordinal = ordinal;
  t->name = name;
  t->kind = kind;
  t->argbytes = argbytes;
  t->ret = ret;
  t->value = value;
  t->fn = fn;
  write_thunk_bytes(g_n_thunks);
  g_n_thunks++;
  return true;
}

uint32_t harness_add_import(const char *module, uint16_t ordinal, const char *name, int kind,
                            uint16_t argbytes, int ret_kind, HApiFn fn) {
  size_t idx = g_n_thunks;
  if (!g_uc || !add_thunk(module, ordinal, name ? strdup(name) : NULL, kind, argbytes, ret_kind, 0, fn))
    return 0;
  return MK_FP(MEM_SEL_THUNK, idx * 8);
}

size_t harness_import_count(void) { return g_n_thunks; }

bool harness_import_info(size_t idx, const char **module, uint16_t *ordinal, const char **name) {
  if (idx >= g_n_thunks) return false;
  if (module) *module = g_thunks[idx].module;
  if (ordinal) *ordinal = g_thunks[idx].ordinal;
  if (name) *name = g_thunks[idx].name;
  return true;
}

uint32_t harness_thunk_far(const char *module, uint16_t ordinal) {
  for (size_t i = 0; i < g_n_thunks; i++) {
    if (g_thunks[i].ordinal == ordinal && strcmp(g_thunks[i].module, module) == 0)
      return MK_FP(MEM_SEL_THUNK, i * 8);
  }
  return 0;
}

void harness_register(const char *module, uint16_t ordinal, HApiFn fn) {
  for (size_t i = 0; i < g_n_handlers; i++) {
    if (g_handlers[i].ordinal == ordinal && strcmp(g_handlers[i].module, module) == 0) {
      g_handlers[i].fn = fn;
      return;
    }
  }
  if (g_n_handlers >= MAX_HANDLERS) {
    fail("too many handlers");
    abort();
  }
  snprintf(g_handlers[g_n_handlers].module, sizeof g_handlers[0].module, "%s", module);
  g_handlers[g_n_handlers].ordinal = ordinal;
  g_handlers[g_n_handlers].fn = fn;
  g_n_handlers++;
}

static HApiFn find_handler(const Thunk *t) {
  for (size_t i = 0; i < g_n_handlers; i++) {
    if (g_handlers[i].ordinal == t->ordinal && strcmp(g_handlers[i].module, t->module) == 0)
      return g_handlers[i].fn;
  }
  return t->fn;
}

bool harness_import_has_handler(size_t idx) {
  return idx < g_n_thunks && find_handler(&g_thunks[idx]) != NULL;
}

/* ---- log ---- */

static HLogEntry *log_new(void);

size_t harness_log_count(void) { return g_n_log; }
const HLogEntry *harness_log_get(size_t i) { return i < g_n_log ? &g_log[i] : NULL; }
void harness_log_clear(void) { g_n_log = 0; }

void harness_log_set(const HLogEntry *e, size_t n) {
  g_n_log = 0;
  for (size_t i = 0; i < n; i++) *log_new() = e[i];
}

size_t harness_log_begin(const char *module, uint16_t ordinal, const uint16_t *words, int nwords) {
  HLogEntry *e = log_new();
  size_t idx = g_n_log - 1;
  e->module = module;
  e->ordinal = ordinal;
  for (size_t i = 0; i < g_n_thunks; i++) {
    if (g_thunks[i].ordinal == ordinal && strcmp(g_thunks[i].module, module) == 0) {
      e->name = g_thunks[i].name;
      break;
    }
  }
  e->argbytes = (uint16_t)(nwords * 2);
  for (int i = 0; i < nwords; i++) {
    unsigned at = (unsigned)(nwords - 1 - i) * 2u; /* the last pushed word is at the lowest address */
    if (at + 2 <= HARNESS_MAX_LOG_ARGS) {
      e->args[at] = (uint8_t)words[i];
      e->args[at + 1] = (uint8_t)(words[i] >> 8);
    }
  }
  e->implemented = true;
  return idx;
}

void harness_log_end(size_t idx, uint32_t ret) {
  if (idx < g_n_log) g_log[idx].ret = ret;
}

static HLogEntry *log_new(void) {
  if (g_n_log == g_cap_log) {
    size_t cap = g_cap_log ? g_cap_log * 2 : 256;
    HLogEntry *n = realloc(g_log, cap * sizeof *n);
    if (!n) abort();
    g_log = n;
    g_cap_log = cap;
  }
  memset(&g_log[g_n_log], 0, sizeof g_log[0]);
  return &g_log[g_n_log++];
}

/* ---- dispatcher ---- */

uint16_t hcall_word(const HCall *c, unsigned n) {
  if (n * 2u + 2u > 0xFFFFu) return 0;
  return (uint16_t)(*(uint16_t *)LIN(c->args + n * 2u));
}

static void on_intr(uc_engine *uc, uint32_t intno, void *ud) {
  Regs16 r, before;
  const Thunk *t;
  HCall c;
  HLogEntry *e;
  size_t log_idx;
  HApiFn fn;
  uint32_t sp_lin;
  (void)ud;
  get_regs(&r);
  if (intno != 0xFE || r.cs != MEM_SEL_THUNK || r.ip < 2 || ((r.ip - 2u) & 7u) != 0 ||
      (size_t)((r.ip - 2u) >> 3) >= g_n_thunks) {
    fprintf(stderr, "harness: unexpected INT 0x%x at %04x:%04x\n", intno, r.cs, r.ip);
    g_last_err = -1;
    uc_emu_stop(uc);
    return;
  }
  t = &g_thunks[(r.ip - 2u) >> 3];
  sp_lin = desc_base(r.ss) + r.sp;
  memset(&c, 0, sizeof c);
  c.module = t->module;
  c.name = t->name;
  c.ordinal = t->ordinal;
  c.kind = t->kind;
  c.ret_kind = t->ret;
  c.argbytes = t->argbytes;
  c.regs = &r;
  c.args = MK_FP(r.ss, (uint16_t)(r.sp + 4));
  before = r;

  e = log_new();
  log_idx = g_n_log - 1;
  e->module = t->module;
  e->ordinal = t->ordinal;
  e->name = t->name;
  e->argbytes = t->argbytes;
  {
    unsigned n = t->argbytes < HARNESS_MAX_LOG_ARGS ? t->argbytes : HARNESS_MAX_LOG_ARGS;
    if ((size_t)sp_lin + 4 + n <= mem_size()) memcpy(e->args, mem_arena() + sp_lin + 4, n);
  }

  fn = find_handler(t);
  if (fn) {
    fn(&c);
    e = &g_log[log_idx]; /* the log may have grown during a nested call */
    e->implemented = true;
  } else {
    if (g_strict) {
      fprintf(stderr, "harness: UNIMPLEMENTED %s.%s (strict mode)\n", t->module,
              t->name ? t->name : "?");
      abort();
    }
    if (!g_quiet)
      fprintf(stderr, "harness: UNIMPLEMENTED %s.%s\n", t->module, t->name ? t->name : "?");
    c.ret = 0;
  }
  e->ret = c.ret;

  if (t->ret == HR_RET16 || t->ret == HR_RET32) r.ax = (uint16_t)c.ret;
  if (t->ret == HR_RET32) r.dx = (uint16_t)(c.ret >> 16);
  if (r.ax != before.ax) wr(UC_X86_REG_AX, r.ax);
  if (r.bx != before.bx) wr(UC_X86_REG_BX, r.bx);
  if (r.cx != before.cx) wr(UC_X86_REG_CX, r.cx);
  if (r.dx != before.dx) wr(UC_X86_REG_DX, r.dx);
  if (r.si != before.si) wr(UC_X86_REG_SI, r.si);
  if (r.di != before.di) wr(UC_X86_REG_DI, r.di);
  if (r.bp != before.bp) wr(UC_X86_REG_BP, r.bp);
  if (r.flags != before.flags) {
    /* Only the arithmetic and direction flags; IF/TF/etc. stay as they are. */
    const uint32_t mask = 0x0CD5u; /* CF PF AF ZF SF DF OF */
    uint32_t fl = 0;
    rd(UC_X86_REG_EFLAGS, &fl);
    fl = (fl & ~mask) | (r.flags & mask);
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &fl);
  }
  if (r.ds != before.ds || r.es != before.es) {
    /* uc_reg_write of DS/ES does not load the descriptor cache in protected
     * mode. Hand the values to the reload stub, which does real segment loads
     * and then jumps to the thunk's RETF. */
    uint8_t *v = mem_arena() + g_thunk_base + THUNK_RELOAD_VAR;
    wr16(v + 0, r.ds);
    wr16(v + 2, r.es);
    wr16(v + 4, r.ip);
    wr(UC_X86_REG_IP, THUNK_RELOAD_OFF);
  }

  g_api_count++;
  if (g_api_limit && g_api_count >= g_api_limit && g_depth == 1) {
    g_stopped_early = true;
    uc_emu_stop(uc);
  }
}

void harness_stop_after_api(unsigned n) {
  g_api_limit = n;
  g_api_count = 0;
}
bool harness_stopped_early(void) { return g_stopped_early; }
unsigned harness_stop_after_api_remaining(void) { return g_api_limit > g_api_count ? g_api_limit - g_api_count : 0; }
void harness_regs(Regs16 *out) { get_regs(out); }
int harness_last_error(void) { return g_last_err; }
void harness_set_fp_trap_check(bool on) { g_fp_trap_check = on; }
uint16_t harness_fp_trap_pending(void) { return g_fp_trap_pending; }
uint16_t harness_fpcw(void) {
  uint16_t v = 0;
  if (g_uc) uc_reg_read(g_uc, UC_X86_REG_FPCW, &v);
  return v;
}
void harness_set_fpcw(uint16_t v) {
  if (g_uc) uc_reg_write(g_uc, UC_X86_REG_FPCW, &v);
}
uint16_t harness_fpsw(void) {
  uint16_t v = 0;
  if (g_uc) uc_reg_read(g_uc, UC_X86_REG_FPSW, &v);
  return v;
}
void harness_set_fpsw(uint16_t v) {
  if (g_uc) uc_reg_write(g_uc, UC_X86_REG_FPSW, &v);
}
void harness_stop_emu(void) {
  if (g_uc) uc_emu_stop(g_uc);
}
void harness_abort_run(void) {
  g_abort = true;
  if (g_uc) uc_emu_stop(g_uc);
}
uint16_t harness_stack_top(void) { return g_stack_top; }
uint16_t harness_stack_limit(void) { return g_stack_limit; }
void harness_set_call_sp(uint16_t sp) { g_call_sp = sp; }
uint16_t harness_get_call_sp(void) { return g_call_sp; }
bool harness_near_conv(int conv) { return conv == HCONV_NEAR_PASCAL || conv == HCONV_NEAR_CDECL; }
uint16_t harness_near_return_offset(uint16_t sel) {
  uint32_t lim = desc_limit(sel);
  return lim < 0xFFFFu ? (uint16_t)(lim + 1u) : 0xFFFFu;
}
int harness_depth(void) { return g_depth; }

static void on_code(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
  (void)uc; (void)size; (void)ud;
  if (g_trace_fn) g_trace_fn((uint32_t)addr, g_trace_ud);
}

/* Adding or removing the code hook only changes the translation blocks that hold an instruction
 * of the hooked range (Unicorn emits the hook call per instruction whose address is in the
 * range), so only those blocks are dropped (uc_ctl_remove_cache over the range, which drops
 * every block overlapping it), not the whole cache: the other blocks are reused by the next
 * run instead of being translated again (stars-a7j). */
static uint32_t g_trace_begin, g_trace_end;

void harness_trace(uint32_t lin_begin, uint32_t lin_end, HTraceFn fn, void *ud) {
  if (!g_uc) return;
  if (g_trace_on) {
    uc_hook_del(g_uc, g_trace_hook);
    g_trace_on = false;
    uc_ctl_remove_cache(g_uc, g_trace_begin, g_trace_end);
  }
  g_trace_fn = fn;
  g_trace_ud = ud;
  if (!fn || lin_end <= lin_begin) return;
  if (uc_hook_add(g_uc, &g_trace_hook, UC_HOOK_CODE, (void *)on_code, NULL, lin_begin, lin_end - 1) ==
      UC_ERR_OK) {
    g_trace_on = true;
    g_trace_begin = lin_begin;
    g_trace_end = lin_end;
    uc_ctl_remove_cache(g_uc, g_trace_begin, g_trace_end);
  }
}
/* FYL2X (D9 F1) and F2XM1 (D9 F0) on the oracle: computed with fp80_fyl2x and fp80_f2xm1, the
 * helpers the C runtime's pow uses (owner decision 2026-10-20, stars-nf6), instead of Unicorn's
 * own helpers, which call the host libm (QEMU 5.0: log(x) / log(2.0) * y, pow(2.0, x) - 1.0).
 * The hook runs before the instruction, reads ST(0) / ST(1), computes the result under the
 * emulated control word (fp80_set_cw(FPCW) for the call, then the fp80 word is restored),
 * writes it like the QEMU helper does (FYL2X: ST(1) = result, then pop: tag of the old top
 * empty, TOP + 1; F2XM1: ST(0) = result; the status word flags are not changed, as in QEMU
 * 5.0), and moves IP past the 2-byte instruction so Unicorn does not run it. Operands outside
 * the helpers' domain (FYL2X: x not a positive normal double or y not finite; F2XM1: |x| > 1
 * or not finite) are left to Unicorn. docs/CONVENTIONS.md "Floating point" rule 5. */
static void on_x87_transc(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
  uint8_t op[2];
  Fp80 st0, st1, r;
  uint16_t cw, sw, tag, ip, saved_cw;
  uint_fast8_t saved_flags;
  uint64_t b0, b1;
  int top;
  (void)size; (void)ud;
  if (uc_mem_read(uc, addr, op, 2) != UC_ERR_OK || op[0] != 0xD9 || (op[1] != 0xF1 && op[1] != 0xF0)) return;
  uc_reg_read(uc, UC_X86_REG_ST0, &st0);
  uc_reg_read(uc, UC_X86_REG_FPCW, &cw);
  saved_cw = fp80_get_cw();
  saved_flags = softfloat_exceptionFlags;
  fp80_set_cw(cw);
  b0 = extF80_to_f64(st0).v; /* the domain check, on the double the helper converts to */
  if (op[1] == 0xF1) {
    uint32_t e0 = (uint32_t)(b0 >> 52) & 0x7FF;
    uc_reg_read(uc, UC_X86_REG_ST1, &st1);
    b1 = extF80_to_f64(st1).v;
    softfloat_exceptionFlags = saved_flags;
    if ((b0 >> 63) || e0 == 0 || e0 == 0x7FF || ((b1 >> 52) & 0x7FF) == 0x7FF) {
      fp80_set_cw(saved_cw);
      return;
    }
    r = fp80_fyl2x(st0, st1);
    uc_reg_write(uc, UC_X86_REG_ST1, &r);
    uc_reg_read(uc, UC_X86_REG_FPSW, &sw);
    uc_reg_read(uc, UC_X86_REG_FPTAG, &tag);
    top = (sw >> 11) & 7;
    tag = (uint16_t)(tag | (3u << (2 * top)));
    sw = (uint16_t)((sw & ~0x3800u) | ((unsigned)((top + 1) & 7) << 11));
    uc_reg_write(uc, UC_X86_REG_FPTAG, &tag);
    uc_reg_write(uc, UC_X86_REG_FPSW, &sw);
  } else {
    softfloat_exceptionFlags = saved_flags;
    if ((b0 & 0x7FFFFFFFFFFFFFFFull) > 0x3FF0000000000000ull) {
      fp80_set_cw(saved_cw);
      return;
    }
    r = fp80_f2xm1(st0);
    uc_reg_write(uc, UC_X86_REG_ST0, &r);
  }
  fp80_set_cw(saved_cw);
  uc_reg_read(uc, UC_X86_REG_IP, &ip);
  ip = (uint16_t)(ip + 2);
  uc_reg_write(uc, UC_X86_REG_IP, &ip);
}

int harness_hook_x87_transc(uint32_t lin) {
  uc_hook h;
  if (!g_uc) return -1;
  if (uc_hook_add(g_uc, &h, UC_HOOK_CODE, (void *)on_x87_transc, NULL, lin, lin) != UC_ERR_OK) return -1;
  uc_ctl_flush_tb(g_uc);
  return 0;
}

/* The FYL2X and F2XM1 instructions of the original (segment 34; the raw D9 F1 matches in
 * segments 6 and 8 are data, docs/inventory.md). Each is checked against the loaded bytes. */
static int hook_seg34_transc(void) {
  static const struct { uint16_t off; uint8_t op; } sites[] = {{0x1367, 0xF1}, {0x137a, 0xF1}, {0x140d, 0xF0}};
  uint32_t base = desc_base(MEM_SEL_SEG(34));
  for (size_t i = 0; i < sizeof sites / sizeof sites[0]; i++) {
    const uint8_t *p = mem_arena() + base + sites[i].off;
    if (p[0] != 0xD9 || p[1] != sites[i].op || harness_hook_x87_transc(base + sites[i].off) != 0) {
      fail("segment 34 FYL2X / F2XM1 site not found");
      return -1;
    }
  }
  return 0;
}

void harness_set_winflags(uint16_t flags) { g_winflags = flags; }
void harness_set_strict(bool strict) { g_strict = strict; }
bool harness_is_strict(void) { return g_strict; }
void harness_set_quiet(bool quiet) { g_quiet = quiet; }

/* ---- calls ---- */

uint32_t harness_call(uint16_t sel, uint16_t off, int conv, const uint16_t *args, int nargs,
                      Regs16 *io) {
  uc_context *ctx = NULL;
  bool nested = g_depth > 0;
  bool near = harness_near_conv(conv);
  bool cdecl = conv == HCONV_CDECL || conv == HCONV_NEAR_CDECL;
  uint16_t ss, sp, ret_off = near ? harness_near_return_offset(sel) : THUNK_RET_OFF;
  uint16_t ret_sel = near ? sel : MEM_SEL_THUNK, call_sp = g_call_sp;
  uint8_t *base;
  uint32_t res = 0, until;
  Regs16 out;
  uc_err err;

  if (!g_uc || g_stack_top == 0) {
    fail("harness_call before harness_load");
    return 0;
  }
  if (near && desc_limit(sel) >= 0xFFFFu) {
    fail("NEAR call into a 64 KB code segment is not supported (its stop offset 0xFFFF is inside the segment)");
    g_last_err = UC_ERR_ARG;
    return 0;
  }
  if (nested && g_abort) return 0; /* the run is being aborted: no new game code */
  if (nested) {
    uc_context_alloc(g_uc, &ctx);
    uc_context_save(g_uc, ctx);
    rd(UC_X86_REG_SS, &ss);
    rd(UC_X86_REG_SP, &sp);
    /* An explicit call SP (a decompiled C frame lives between the emulator's
     * SP and the callee) wins over the emulator's SP; the stack segment is the
     * emulator's current one, which is where the C frame was built. */
    if (call_sp) sp = call_sp;
  } else {
    ss = MEM_SEL_DGROUP;
    sp = call_sp ? call_sp : g_stack_top;
    g_stopped_early = false;
    g_abort = false;
    g_api_count = 0;
    g_last_err = 0;
    g_fp_trap_pending = 0;
    if (g_fp_trap_check) {
      uint16_t sw = harness_fpsw();
      harness_set_fpsw((uint16_t)(sw & ~0x803Fu)); /* clear ES and the exception flags */
    }
    wr(UC_X86_REG_AX, 0); wr(UC_X86_REG_BX, 0); wr(UC_X86_REG_CX, 0); wr(UC_X86_REG_DX, 0);
    wr(UC_X86_REG_SI, 0); wr(UC_X86_REG_DI, 0); wr(UC_X86_REG_BP, 0); wr(UC_X86_REG_ES, 0);
  }
  base = mem_arena() + desc_base(ss);

#define PUSH16(v)                                      \
  do {                                                 \
    sp = (uint16_t)(sp - 2);                           \
    base[sp] = (uint8_t)((v) & 0xFF);                  \
    base[sp + 1u] = (uint8_t)(((v) >> 8) & 0xFF);      \
  } while (0)
  if (cdecl) {
    for (int i = nargs - 1; i >= 0; i--) PUSH16(args[i]);
  } else {
    for (int i = 0; i < nargs; i++) PUSH16(args[i]);
  }
  if (!near) PUSH16(MEM_SEL_THUNK);
  PUSH16(ret_off);
#undef PUSH16

  if (io) {
    wr(UC_X86_REG_AX, io->ax); wr(UC_X86_REG_BX, io->bx); wr(UC_X86_REG_CX, io->cx);
    wr(UC_X86_REG_DX, io->dx); wr(UC_X86_REG_SI, io->si); wr(UC_X86_REG_DI, io->di);
    wr(UC_X86_REG_BP, io->bp);
  }
  {
    uint16_t es = 0;
    uint8_t *v = mem_arena() + g_thunk_base + THUNK_VAR_OFF;
    if (io && io->es) es = io->es;
    else rd(UC_X86_REG_ES, &es);
    wr16(v + 0, off);
    wr16(v + 2, sel);
    wr16(v + 4, MEM_SEL_DGROUP);
    wr16(v + 6, es);
    wr16(v + 8, ss);
    wr16(v + 10, sp);
    /* Entry flags. uc_reg_write(EFLAGS) is lost for the arithmetic flags
     * (Unicorn reloads them from its stale saved copy when emulation starts),
     * so the call stub loads them with POPF from this variable. */
    {
      uint32_t fl = 0;
      const uint32_t fmask = 0x0CD5u; /* CF PF AF ZF SF DF OF, as for the API handlers */
      rd(UC_X86_REG_EFLAGS, &fl);
      if (io) fl = (fl & ~fmask) | (io->flags & fmask);
      wr16(v + 12, (uint16_t)(fl | 2u));
    }
  }

  until = near ? desc_base(sel) + ret_off : g_thunk_base + THUNK_RET_OFF;
  g_call_sp = 0; /* applies to this call only, not to calls made by handlers */
  g_depth++;
  {
    /* In UC_MODE_16 uc_emu_start sets IP = begin - CS * 16 (the real-mode rule), whatever the
     * descriptor base of CS is. CS is the thunk selector here (every run, and every INT 0xFE
     * hook that makes a nested call, is inside the thunk segment), so begin = CS * 16 +
     * THUNK_CALL_OFF starts exactly at the call stub. Passing the linear address of the stub
     * (g_thunk_base + THUNK_CALL_OFF) started MEM_SEL_THUNK * 16 = 0x1200 bytes too early: 2304
     * "ADD [BX+SI],AL" over the zero bytes before the stub, a net no-op (2304 * AL = 0 mod 256,
     * and the stub's POPF reloads the flags) that cost about 60 us a call (stars-a7j). */
    uint16_t cs = 0;
    rd(UC_X86_REG_CS, &cs);
    err = uc_emu_start(g_uc, (uint64_t)cs * 16u + THUNK_CALL_OFF, until, 0, 0);
  }
  g_depth--;
  g_call_sp = call_sp;
  if (err != UC_ERR_OK) {
    fprintf(stderr, "harness: uc_emu_start failed: %s\n", uc_strerror(err));
    g_last_err = (int)err;
  }

  if (!nested && g_fp_trap_check && g_last_err == 0) {
    uint16_t pend = (uint16_t)(harness_fpsw() & ~harness_fpcw() & 0x3Fu);
    if (pend) {
      g_fp_trap_pending = pend;
      if (!g_quiet)
        fprintf(stderr, "harness: unmasked x87 exception 0x%02x (cw 0x%04x): the real game traps to 34:0x1db4\n",
                pend, harness_fpcw());
      g_last_err = -1;
    }
  }

  get_regs(&out);
  if (cdecl && !g_stopped_early) {
    out.sp = (uint16_t)(out.sp + 2 * nargs);
    wr(UC_X86_REG_SP, out.sp);
  }
  if (io) *io = out;
  if (!g_stopped_early) res = ((uint32_t)out.dx << 16) | out.ax;
  if (nested) {
    uc_context_restore(g_uc, ctx);
    uc_context_free(ctx);
    /* Unicorn 2.1.4 keeps a stale chained translation block after a nested
     * uc_emu_start; the next return into JIT code crashed (seen on arm64
     * macOS). Dropping the translation cache after every nested call avoids
     * it (test_callback_repeated in harness/tests/test_harness.c). */
    uc_ctl_flush_tb(g_uc);
    /* A fatal abort inside this nested run must end the enclosing run too, not only the
     * innermost one (the outer handler's own uc_emu_start is still active). */
    if (g_abort) uc_emu_stop(g_uc);
  } else if (g_abort || (!g_stopped_early && (g_last_err != 0 || out.cs != ret_sel || out.ip != ret_off))) {
    /* The run did not end at the return address: a fault, or a stop from a
     * hook. Report it and put the CPU back to its post-boot state so later
     * calls work. */
    if (g_last_err == 0) g_last_err = -1;
    g_abort = false;
    uc_context_restore(g_uc, g_clean_ctx);
  } else if (near) {
    /* The CPU is left in the callee's code segment; every run must start with
     * CS = the thunk segment (see the notes at the top of this file). */
    uc_context_restore(g_uc, g_clean_ctx);
  }
  return res;
}

uint32_t harness_call_far(uint32_t fp, int conv, const uint16_t *args, int nargs) {
  return harness_call(FP_SEG(fp), FP_OFF(fp), conv, args, nargs, NULL);
}

uint16_t harness_alloc_code(const void *bytes, size_t n) {
  uint16_t h;
  size_t size = n < 16 ? 16 : n;
  if (size > 65536) return 0;
  /* 16 spare bytes past the limit: a NEAR call stops at limit+1, which must not
   * be the next segment's offset 0 (GlobalAlloc packs segments without a gap). */
  h = GlobalAlloc(GPTR, (uint32_t)size + (size < 65536 ? 16u : 0u));
  if (!h) return 0;
  memcpy(mem_arena() + desc_base(h), bytes, n);
  desc_set(h, desc_base(h), (uint32_t)size - 1, 0x9B);
  return h;
}

/* ---- init / shutdown ---- */

int harness_init(void) {
  static const uint8_t boot[] = {
      0x0f, 0x01, 0x16, 0x20, 0x05,                                   /* LGDT [0x520]    */
      0x0f, 0x20, 0xc0, 0x0c, 0x01, 0x0f, 0x22, 0xc0,                /* CR0.PE = 1      */
      0xea, THUNK_IDLE_OFF & 0xFF, THUNK_IDLE_OFF >> 8,
      MEM_SEL_THUNK & 0xFF, MEM_SEL_THUNK >> 8,                       /* JMP FAR thunk:idle */
  };
  uint16_t h, zero = 0;
  uint8_t *a;
  uc_err err;
  uc_hook hh;

  if (g_uc) harness_shutdown();
  mem_init(0);
  a = mem_arena();

  h = GlobalAlloc(GPTR, 65536);
  if (!h) {
    fail("cannot allocate the thunk segment");
    return -1;
  }
  g_thunk_base = desc_base(h);
  desc_set(MEM_SEL_THUNK, g_thunk_base, 0xFFFF, 0x9B);
  {
    uint8_t *t = a + g_thunk_base;
#define V(k) ((THUNK_VAR_OFF + (k)) & 0xFF), ((THUNK_VAR_OFF + (k)) >> 8)
    static const uint8_t call_stub[] = {
        0x2e, 0x8e, 0x1e, V(4),  /* MOV DS,CS:[var+4] */
        0x2e, 0x8e, 0x06, V(6),  /* MOV ES,CS:[var+6] */
        0x2e, 0x8e, 0x16, V(8),  /* MOV SS,CS:[var+8] */
        0x2e, 0x8b, 0x26, V(10), /* MOV SP,CS:[var+10] */
        0x2e, 0xff, 0x36, V(12), /* PUSH CS:[var+12] (entry flags) */
        0x9d,                    /* POPF */
        0x2e, 0xff, 0x2e, V(0),  /* JMP FAR CS:[var] */
    };
#undef V
    memcpy(t + THUNK_CALL_OFF, call_stub, sizeof call_stub);
    t[THUNK_RET_OFF] = 0xF4;                                 /* HLT, never executed */
    t[THUNK_IDLE_OFF] = 0xF4;
#define R(k) ((THUNK_RELOAD_VAR + (k)) & 0xFF), ((THUNK_RELOAD_VAR + (k)) >> 8)
    {
      static const uint8_t reload_stub[] = {
          0x2e, 0x8e, 0x1e, R(0), /* MOV DS,CS:[var]   */
          0x2e, 0x8e, 0x06, R(2), /* MOV ES,CS:[var+2] */
          0x2e, 0xff, 0x26, R(4), /* JMP NEAR CS:[var+4] */
      };
      memcpy(t + THUNK_RELOAD_OFF, reload_stub, sizeof reload_stub);
    }
#undef R
  }
  memcpy(a + MEM_BOOT_LINEAR, boot, sizeof boot);
  {
    static const uint8_t gdtr[6] = {0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00}; /* limit 0xFFFF, base 0x10000 */
    memcpy(a + 0x520, gdtr, sizeof gdtr);
  }

  err = uc_open(UC_ARCH_X86, UC_MODE_16, &g_uc);
  if (err != UC_ERR_OK) {
    fail(uc_strerror(err));
    return -1;
  }
  err = uc_mem_map_ptr(g_uc, 0, mem_size(), UC_PROT_ALL, a);
  if (err != UC_ERR_OK) {
    fail(uc_strerror(err));
    return -1;
  }
  uc_hook_add(g_uc, &hh, UC_HOOK_INTR, (void *)on_intr, NULL, 1, 0);

  wr(UC_X86_REG_CS, zero);
  wr(UC_X86_REG_DS, zero);
  err = uc_emu_start(g_uc, MEM_BOOT_LINEAR, g_thunk_base + THUNK_IDLE_OFF, 0, 0);
  if (err != UC_ERR_OK) {
    fprintf(stderr, "harness: boot failed: %s\n", uc_strerror(err));
    return -1;
  }
  /* Unicorn starts with FPCW = 0 (24-bit precision), which no real x87 has after
   * reset. Load FPU_CW_DEFAULT (runtime/mem.h, owner decision 0x1330: PC = 11b, IM/DM/ZM/OM
   * unmasked, handler 34:0x1db4). The C runtime start-up path saves the word (FNSTCW),
   * runs FNINIT and restores it (FLDCW at 34:0x26b1), so it is net-neutral and whatever
   * word is set here reaches WinMain. docs/CONVENTIONS.md "Floating point". */
  harness_set_fpcw(FPU_CW_DEFAULT);
  g_n_thunks = 0;
  g_depth = 0;
  g_last_err = 0;
  g_n_log = 0;
  g_api_limit = g_api_count = 0;
  g_stopped_early = false;
  g_abort = false;
  if (uc_context_alloc(g_uc, &g_clean_ctx) != UC_ERR_OK || uc_context_save(g_uc, g_clean_ctx) != UC_ERR_OK) {
    fail("cannot save the clean context");
    return -1;
  }
  return 0;
}

void harness_shutdown(void) {
  if (g_clean_ctx) uc_context_free(g_clean_ctx);
  g_clean_ctx = NULL;
  if (g_uc) uc_close(g_uc);
  g_uc = NULL;
  g_n_thunks = 0;
  g_n_handlers = 0;
  free(g_log);
  g_log = NULL;
  g_n_log = g_cap_log = 0;
  g_stack_top = g_stack_limit = g_call_sp = 0;
  g_trace_on = false;
  g_trace_fn = NULL;
  mem_shutdown();
}

/* ---- loader ---- */

typedef struct {
  uint16_t mod, ord;
} ImpKey;

static int cmp_key(const void *a, const void *b) {
  const ImpKey *x = a, *y = b;
  if (x->mod != y->mod) return x->mod < y->mod ? -1 : 1;
  if (x->ord != y->ord) return x->ord < y->ord ? -1 : 1;
  return 0;
}

static int build_thunks(const NeFile *f, ImpKey **keys_out, size_t *n_out) {
  ImpKey *keys = malloc((f->n_sites + 1) * sizeof *keys);
  size_t n = 0, u = 0;
  if (!keys) return -1;
  for (size_t i = 0; i < f->n_sites; i++) {
    if (f->sites[i].kind != NE_FIX_ORDINAL) continue;
    keys[n].mod = f->sites[i].w1;
    keys[n].ord = f->sites[i].w2;
    n++;
  }
  qsort(keys, n, sizeof *keys, cmp_key);
  for (size_t i = 0; i < n; i++) {
    if (u == 0 || cmp_key(&keys[u - 1], &keys[i]) != 0) keys[u++] = keys[i];
  }
  for (size_t i = 0; i < u; i++) {
    const char *mn = ne_module_name(f, keys[i].mod);
    char up[16];
    const ApiDef *d;
    size_t k = 0;
    if (!mn) {
      fail("import with bad module index");
      free(keys);
      return -1;
    }
    for (; mn[k] && k + 1 < sizeof up; k++) up[k] = (char)((mn[k] >= 'a' && mn[k] <= 'z') ? mn[k] - 32 : mn[k]);
    up[k] = 0;
    d = find_def(up, keys[i].ord);
    if (!d) {
      fprintf(stderr, "harness: import %s.%u is not in win16_api.inc\n", up, keys[i].ord);
      free(keys);
      return -1;
    }
    {
      uint16_t value = d->value;
      if (strcmp(d->name, "__WINFLAGS") == 0) value = g_winflags;
      if (!add_thunk(up, d->ordinal, d->name, d->kind, d->argbytes, d->ret, value, NULL)) {
        fail("too many imports");
        free(keys);
        return -1;
      }
    }
  }
  *keys_out = keys;
  *n_out = u;
  return 0;
}

static int apply_sites(const NeFile *f, const ImpKey *keys, size_t nkeys) {
  for (size_t i = 0; i < f->n_sites; i++) {
    const NeFixup *s = &f->sites[i];
    uint16_t sel_seg = (uint16_t)(s->seg << 3);
    uint8_t *seg = mem_arena() + desc_base(sel_seg);
    uint32_t seg_size = desc_limit(sel_seg) + 1u;
    uint16_t tsel = 0, toff = 0, addend = 0;
    bool equ = false;
    uint32_t width = s->src == NE_SRC_LOBYTE ? 1 : s->src == NE_SRC_FAR_ADDR ? 4 : 2;

    if (s->kind == NE_FIX_OSFIXUP) continue; /* sites already hold native x87 code */
    if (s->site + width > seg_size) {
      fprintf(stderr, "harness: fixup site %u:%04x out of range\n", s->seg, s->site);
      return -1;
    }
    if (s->additive && width >= 2) addend = rd16(seg + s->site);
    if (s->kind == NE_FIX_INTERNAL) {
      if (s->w1 == 0xFF) {
        if (s->w2 == 0 || s->w2 > f->n_entries) return -1;
        tsel = (uint16_t)(f->entries[s->w2 - 1].seg << 3);
        toff = f->entries[s->w2 - 1].off;
      } else {
        tsel = (uint16_t)(s->w1 << 3);
        toff = s->w2;
      }
    } else if (s->kind == NE_FIX_ORDINAL) {
      ImpKey k = {s->w1, s->w2};
      const ImpKey *hit = bsearch(&k, keys, nkeys, sizeof k, cmp_key);
      const Thunk *t;
      if (!hit) return -1;
      t = &g_thunks[hit - keys];
      tsel = MEM_SEL_THUNK;
      toff = (uint16_t)((hit - keys) * 8);
      if (t->kind == HK_EQU) {
        equ = true;
        toff = t->value;
        tsel = t->value;
      }
    } else {
      fprintf(stderr, "harness: unsupported fixup kind %u at %u:%04x\n", s->kind, s->seg, s->site);
      return -1;
    }
    switch (s->src) {
      case NE_SRC_LOBYTE:
        seg[s->site] = (uint8_t)(toff + addend);
        break;
      case NE_SRC_SEGMENT:
        wr16(seg + s->site, tsel);
        break;
      case NE_SRC_FAR_ADDR:
        if (equ) return -1;
        wr16(seg + s->site, (uint16_t)(toff + addend));
        wr16(seg + s->site + 2, tsel);
        break;
      case NE_SRC_OFFSET:
        wr16(seg + s->site, (uint16_t)(toff + addend));
        break;
      default:
        fprintf(stderr, "harness: unsupported fixup source %u\n", s->src);
        return -1;
    }
  }
  return 0;
}

int harness_load(const char *exe_path) {
  FILE *fp;
  long len;
  uint8_t *buf;
  NeFile f;
  ImpKey *keys = NULL;
  size_t nkeys = 0;
  int rc = -1;

  if (!g_uc && harness_init() != 0) return -1;
  fp = fopen(exe_path, "rb");
  if (!fp) {
    fprintf(stderr, "harness: cannot open %s\n", exe_path);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  buf = malloc((size_t)len);
  if (!buf || fread(buf, 1, (size_t)len, fp) != (size_t)len) {
    fclose(fp);
    free(buf);
    return -1;
  }
  fclose(fp);
  if (ne_parse(buf, (size_t)len, &f) != NE_OK) {
    fail("not a valid NE file");
    free(buf);
    return -1;
  }

  for (size_t n = 1; n <= f.n_segs; n++) {
    const NeSegment *s = &f.segs[n - 1];
    uint32_t size = s->length > s->min_alloc ? s->length : s->min_alloc;
    uint8_t access = (s->flags & 1) ? 0x93 : 0x9B;
    uint16_t h;
    if (n == f.hdr.auto_data_seg) {
      g_stack_limit = (uint16_t)size;
      g_stack_top = (uint16_t)((size + f.hdr.stack_size) & ~1u);
      size += f.hdr.stack_size + f.hdr.heap_size;
    }
    if (size > 65536) {
      fail("segment too large");
      goto done;
    }
    /* code segments get 16 spare bytes past the limit (see harness_alloc_code) */
    h = GlobalAlloc(GPTR, size + (!(s->flags & 1) && size < 65536 ? 16u : 0u));
    if (!h) {
      fail("out of memory for segments");
      goto done;
    }
    if (s->file_off) memcpy(mem_arena() + desc_base(h), buf + s->file_off, s->length);
    desc_set((uint16_t)(n << 3), desc_base(h), size - 1, access);
  }
  if (build_thunks(&f, &keys, &nkeys) != 0) goto done;
  if (apply_sites(&f, keys, nkeys) != 0) goto done;
  /* Not idempotent: the hooks live on g_uc (created by harness_init, not here), so a second
   * harness_load without harness_shutdown adds a second set of hooks; where they land on the same
   * site, each one runs the helper and advances IP again. */
  if (f.n_segs >= 34 && hook_seg34_transc() != 0) goto done;
  if (f.hdr.auto_data_seg != MEM_SEL_DGROUP >> 3) {
    fail("auto data segment is not segment 35");
    goto done;
  }
  rc = 0;
done:
  free(keys);
  ne_free(&f);
  free(buf);
  return rc;
}
