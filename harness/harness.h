/* Unicorn hybrid harness (docs/PLAN.md sections 3 and 4). Native builds only:
 * never compile this directory into the wasm build.
 *
 * The harness maps the runtime arena (runtime/mem.c) into a 16-bit Unicorn
 * engine, loads the NE segments of the original exe into the arena at their
 * fixed selectors, and routes every import to a C handler through INT 0xFE
 * thunks in the thunk segment (selector MEM_SEL_THUNK).
 *
 * Argument words for harness_call are given in push order for PASCAL (first
 * declared argument first; a 32-bit argument is two words, high word first)
 * and in declaration order for CDECL (the first element ends up lowest on the
 * stack; a 32-bit argument is two words, low word first).
 */
#ifndef STARS_HARNESS_HARNESS_H
#define STARS_HARNESS_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime/mem.h"

typedef struct Regs16 {
  uint16_t ax, bx, cx, dx, si, di, bp, sp, ds, es, ss, cs, ip, flags;
} Regs16;

/* The NEAR variants push a 2-byte return offset (no selector) and the call ends
 * when the callee returns to that offset in its own code segment (see
 * harness_near_return_offset). */
enum { HCONV_PASCAL = 0, HCONV_CDECL = 1, HCONV_NEAR_PASCAL = 2, HCONV_NEAR_CDECL = 3 };

/* Import kinds and return kinds, as in runtime/win16_api.inc. */
enum { HK_PASCAL, HK_REGISTER, HK_VARARGS, HK_STUB, HK_VARIABLE, HK_EQU };
enum { HR_NONE, HR_RET16, HR_RET32 };

/* __WINFLAGS default: WINFLAGS_DEFAULT in runtime/mem.h (owner-decided). */

#define HARNESS_MAX_LOG_ARGS 32

typedef struct HCall HCall;
typedef void (*HApiFn)(HCall *c);

/* One API invocation as seen by a handler. */
struct HCall {
  const char *module;
  const char *name;       /* NULL when the ordinal has no name */
  uint16_t ordinal;
  int kind;               /* HK_* */
  int ret_kind;           /* HR_* */
  uint16_t argbytes;      /* bytes the callee pops (fixed part for VARARGS) */
  Regs16 *regs;           /* registers at the call; changes are written back */
  uint32_t args;          /* far pointer to the first argument byte (above the return address) */
  uint32_t ret;           /* handler sets this: AX for HR_RET16, DX:AX for HR_RET32 */
};

/* Argument access. word(c, n) is the n-th 16-bit word at the argument base
 * (word 0 = the last pushed word of a PASCAL call). */
uint16_t hcall_word(const HCall *c, unsigned n);

typedef struct HLogEntry {
  const char *module;
  uint16_t ordinal;
  const char *name;
  uint16_t argbytes;
  uint8_t args[HARNESS_MAX_LOG_ARGS]; /* first min(argbytes, 32) raw argument bytes */
  uint32_t ret;
  bool implemented;
} HLogEntry;

/* Lifecycle. harness_init builds the arena (mem_init(0)), the engine, the boot
 * stub run and the thunk segment; harness_load calls it when needed and then
 * loads the exe. Both return 0 on success, negative on failure (message on
 * stderr). harness_shutdown releases everything including the arena. */
int harness_init(void);
int harness_load(const char *exe_path);
void harness_shutdown(void);

/* Configuration. harness_set_winflags must be called before harness_load. */
void harness_set_winflags(uint16_t flags);
bool harness_is_strict(void);
void harness_set_strict(bool strict);  /* unimplemented API: abort (strict) or log and return 0 */
void harness_set_quiet(bool quiet);    /* suppress the UNIMPLEMENTED stderr line */

/* Handlers, keyed by (module, ordinal). Registering again replaces. */
void harness_register(const char *module, uint16_t ordinal, HApiFn fn);

/* Adds an extra import thunk after the loaded ones (used by tests and by
 * runtime tasks for synthetic imports). Returns its far pointer, 0 on failure. */
uint32_t harness_add_import(const char *module, uint16_t ordinal, const char *name, int kind,
                            uint16_t argbytes, int ret_kind, HApiFn fn);

/* Thunk table, ordered as laid out in the thunk segment (index = offset / 8). */
size_t harness_import_count(void);
bool harness_import_info(size_t idx, const char **module, uint16_t *ordinal, const char **name);
/* True when import idx has a handler registered (strict mode would not abort on it). */
bool harness_import_has_handler(size_t idx);
/* Far pointer of the thunk for (module, ordinal), 0 when not imported. */
uint32_t harness_thunk_far(const char *module, uint16_t ordinal);

/* Calls code at sel:off and runs until it returns (RETF to the return stub).
 * io may be NULL; otherwise its ax..di, bp, es and the arithmetic/direction
 * flags (mask 0x0CD5) are loaded before the call and the registers are stored
 * back afterwards. After a NEAR call the CPU is reset to its post-boot state
 * (only the io copy holds the result registers). Returns DX:AX. Nested calls
 * (from inside an API handler) run on the caller's stack and restore the
 * caller's registers before returning. */
uint32_t harness_call(uint16_t sel, uint16_t off, int conv, const uint16_t *args, int nargs,
                      Regs16 *io);
uint32_t harness_call_far(uint32_t fp, int conv, const uint16_t *args, int nargs);

/* After n more API calls (counted from now), the run stops inside the
 * thunk. harness_call then returns 0 and harness_stopped_early() is true until
 * the next harness_call. n == 0 clears the limit. */
void harness_stop_after_api(unsigned n);
bool harness_stopped_early(void);
/* API calls still allowed before the pending stop_after limit fires (0 when none is pending). */
unsigned harness_stop_after_api_remaining(void);
void harness_regs(Regs16 *out); /* current register file */

/* API call log. */
size_t harness_log_count(void);
const HLogEntry *harness_log_get(size_t i);
void harness_log_clear(void);
/* Replaces the log with a copy of e[0..n) (restores a log saved earlier). */
void harness_log_set(const HLogEntry *e, size_t n);
/* By-name call logging (harness/byname.h): records the call as the import thunk would. words are
 * the argument words in PUSH order (the first one pushed first); the entry's argument bytes are the
 * stack image, so the last pushed word comes first. harness_log_begin appends the entry before the
 * runtime function runs (so any nested logged call comes after it) and returns its index;
 * harness_log_end stores the return value. */
size_t harness_log_begin(const char *module, uint16_t ordinal, const uint16_t *words, int nwords);
void harness_log_end(size_t idx, uint32_t ret);

/* x87 control word of the emulated FPU (UC_X86_REG_FPCW), for the FP semantics
 * tests (docs/CONVENTIONS.md "Floating point"). harness_stop_emu stops the
 * running emulation from a trace callback. */
uint16_t harness_fpcw(void);
void harness_set_fpcw(uint16_t v);
uint16_t harness_fpsw(void); /* x87 status word (UC_X86_REG_FPSW) */
void harness_set_fpsw(uint16_t v);
void harness_stop_emu(void);
/* Unmasked x87 exceptions (owner decision: FPU_CW_DEFAULT = 0x1330, docs/CONVENTIONS.md).
 * Unicorn computes the IEEE result and sets the status flags but does NOT vector to the
 * game's handler (34:0x1db4). With the check on, a top-level harness_call clears the
 * exception flags (IE DE ZE OE UE PE) on entry and, if on return any flag is set whose
 * mask bit in the control word is 0, ends with harness_last_error() != 0 and a stderr line
 * (unless quiet). harness_fp_trap_pending() returns those unmasked flags (x87 status
 * layout: IE 1, DE 2, ZE 4, OE 8, UE 0x10, PE 0x20) of the most recent such call. */
void harness_set_fp_trap_check(bool on);
uint16_t harness_fp_trap_pending(void);
/* Fatal abort: stops the running emulation AND every enclosing one. harness_stop_emu stops
 * only the innermost uc_emu_start; harness_call propagates this sticky abort up through the
 * nested calls (an API handler calling game code), so the outer game code never continues.
 * At depth 0 the call ends with harness_last_error() != 0 and the flag is cleared. */
void harness_abort_run(void);

/* Engine/last error: 0 when the last run ended normally, else a uc_err value. */
int harness_last_error(void);

/* Stack top (SP value) of the DGROUP stack the outermost call starts with, and
 * the lowest stack offset (the end of the static data; the stack grows down to it). */
uint16_t harness_stack_top(void);
uint16_t harness_stack_limit(void);
/* Outermost calls start at this SP instead of the stack top while it is nonzero
 * (the diff-test driver uses it so original code called from decompiled C runs
 * below the C code's emulated frames). 0 restores the default. */
void harness_set_call_sp(uint16_t sp);
uint16_t harness_get_call_sp(void);
/* The call SP also applies to a nested call (from an API handler): it is then
 * the offset in the emulator's current stack segment, and wins over the
 * emulator's SP. It stays set until the caller resets it with
 * harness_set_call_sp(0) (the outermost harness_call does not clear it);
 * calls made by handlers during a run start with no call SP, because
 * harness_call clears it for the duration of the run and restores it after. */
bool harness_near_conv(int conv);
/* Return offset a NEAR call pushes: the first offset past the code segment's
 * limit (0xFFFF for a 64 KB segment). The call ends when the callee returns
 * to it. */
uint16_t harness_near_return_offset(uint16_t sel);
/* Nesting depth of emulation runs: 0 when no code is running. */
int harness_depth(void);
/* Offset in the thunk segment that harness_call pushes as the return address
 * (the return address is MEM_SEL_THUNK:HARNESS_THUNK_RET_OFF). */
#define HARNESS_THUNK_RET_OFF 0xF060u
/* The call/reload variables in the thunk segment ([off, off + len)) are scratch
 * the harness rewrites on every call; diffs of the arena must ignore them. */
#define HARNESS_THUNK_VARS_OFF 0xF040u
#define HARNESS_THUNK_VARS_LEN 0x20u

/* Instruction trace: fn(linear address) is called for every instruction executed
 * in the linear range [lin_begin, lin_end). Passing fn == NULL removes the trace. */
typedef void (*HTraceFn)(uint32_t lin, void *ud);
void harness_trace(uint32_t lin_begin, uint32_t lin_end, HTraceFn fn, void *ud);

/* Makes the FYL2X (D9 F1) or F2XM1 (D9 F0) instruction at linear address lin run through
 * fp80_fyl2x / fp80_f2xm1 (runtime/fp80.c) instead of Unicorn's libm-based helpers, so the
 * oracle and the C runtime's pow agree by construction. harness_load does this for the
 * original's sites in segment 34; tests call it for synthetic code. 0 on success. */
int harness_hook_x87_transc(uint32_t lin);

/* Allocates a code segment holding bytes and returns its selector (0 on failure);
 * for synthetic code in tests. The descriptor is a present 16-bit code segment. */
uint16_t harness_alloc_code(const void *bytes, size_t n);

#endif /* STARS_HARNESS_HARNESS_H */
