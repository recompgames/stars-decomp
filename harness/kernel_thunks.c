/* HCall decoders for the KERNEL part 1 functions of runtime/kernel.h (task stars-0z4.2).
 * Native builds only. Arguments are decoded from the PASCAL stack words: hcall_word(c, 0)
 * is the LAST pushed word, and a far pointer / DWORD is pushed high word first, so its low
 * word is word n and its high word word n+1. */
#include "harness/kernel_thunks.h"

#include <stddef.h>

#include "harness/harness.h"
#include "runtime/kernel.h"
#include "runtime/kernel_file.h"
#include "runtime/mem.h"

static uint32_t w32(const HCall *c, unsigned n) {
  return (uint32_t)hcall_word(c, n) | ((uint32_t)hcall_word(c, n + 1) << 16);
}

static void t_InitTask(HCall *c) {
  KInitTaskRegs r;
  InitTask(&r);
  c->regs->ax = r.ax;
  c->regs->bx = r.bx;
  c->regs->cx = r.cx;
  c->regs->dx = r.dx;
  c->regs->si = r.si;
  c->regs->di = r.di;
  c->regs->es = r.es;
}
static void t_WaitEvent(HCall *c) { c->ret = WaitEvent(hcall_word(c, 0)); }
static void t_Yield(HCall *c) { Yield(); c->ret = 0; }
static void t_GetVersion(HCall *c) { c->ret = GetVersion(); }

static void t_GetModuleFileName(HCall *c) {
  c->ret = GetModuleFileName(hcall_word(c, 3), w32(c, 1), hcall_word(c, 0));
}
static void t_MakeProcInstance(HCall *c) { c->ret = MakeProcInstance(w32(c, 1), hcall_word(c, 0)); }
static void t_FreeProcInstance(HCall *c) { FreeProcInstance(w32(c, 0)); c->ret = 0; }

static void t_GlobalAlloc(HCall *c) { c->ret = GlobalAlloc(hcall_word(c, 2), w32(c, 0)); }
static void t_GlobalReAlloc(HCall *c) {
  c->ret = GlobalReAlloc(hcall_word(c, 3), w32(c, 1), hcall_word(c, 0));
}
static void t_GlobalFree(HCall *c) { c->ret = GlobalFree(hcall_word(c, 0)); }
static void t_GlobalLock(HCall *c) { c->ret = GlobalLock(hcall_word(c, 0)); }
static void t_GlobalUnlock(HCall *c) { c->ret = GlobalUnlock(hcall_word(c, 0)) ? 1 : 0; }
static void t_GlobalSize(HCall *c) { c->ret = GlobalSize(hcall_word(c, 0)); }
static void t_LockSegment(HCall *c) { c->ret = LockSegment(hcall_word(c, 0), c->regs->ds); }
static void t_UnlockSegment(HCall *c) { c->ret = UnlockSegment(hcall_word(c, 0), c->regs->ds); }

static void t_LocalAlloc(HCall *c) { c->ret = kernel_LocalAlloc(hcall_word(c, 1), hcall_word(c, 0)); }
static void t_LocalReAlloc(HCall *c) {
  c->ret = kernel_LocalReAlloc(hcall_word(c, 2), hcall_word(c, 1), hcall_word(c, 0));
}
static void t_LocalFree(HCall *c) { c->ret = LocalFree(hcall_word(c, 0)); }
static void t_LocalSize(HCall *c) { c->ret = LocalSize(hcall_word(c, 0)); }

static void t_FindResource(HCall *c) {
  c->ret = FindResource(hcall_word(c, 4), w32(c, 2), w32(c, 0));
}
static void t_SizeofResource(HCall *c) { c->ret = SizeofResource(hcall_word(c, 1), hcall_word(c, 0)); }
static void t_LoadResource(HCall *c) { c->ret = LoadResource(hcall_word(c, 1), hcall_word(c, 0)); }
static void t_LockResource(HCall *c) { c->ret = LockResource(hcall_word(c, 0)); }
static void t_FreeResource(HCall *c) { c->ret = FreeResource(hcall_word(c, 0)); }
static void t_AllocResource(HCall *c) {
  c->ret = AllocResource(hcall_word(c, 3), hcall_word(c, 2), w32(c, 0));
}

static void t_lstrlen(HCall *c) { c->ret = lstrlen(w32(c, 0)); }
static void t_lstrcpy(HCall *c) { c->ret = lstrcpy(w32(c, 2), w32(c, 0)); }
static void t_lstrcat(HCall *c) { c->ret = lstrcat(w32(c, 2), w32(c, 0)); }

static void t_GetDOSEnvironment(HCall *c) { c->ret = GetDOSEnvironment(); }
static void t_GetDriveType(HCall *c) { c->ret = GetDriveType(hcall_word(c, 0)); }

/* ---- KERNEL part 2 (runtime/kernel_file.h) ---- */

static void stop_if_fatal(void);

static void t_OpenFile(HCall *c) { c->ret = (uint16_t)OpenFile(w32(c, 3), w32(c, 1), hcall_word(c, 0)); }
static void t_lclose(HCall *c) { c->ret = (uint16_t)_lclose((HFILE16)hcall_word(c, 0)); }
static void t_lread(HCall *c) { c->ret = _lread((HFILE16)hcall_word(c, 3), w32(c, 1), hcall_word(c, 0)); }
static void t_lwrite(HCall *c) { c->ret = _lwrite((HFILE16)hcall_word(c, 3), w32(c, 1), hcall_word(c, 0)); }
static void t_AccessResource(HCall *c) {
  c->ret = (uint16_t)AccessResource(hcall_word(c, 1), hcall_word(c, 0));
}

/* DOS3Call is register based: the registers at the call are the INT 21h registers, the
 * results are written back, CF is bit 0 of the flags. */
static void t_DOS3Call(HCall *c) {
  KDosRegs r;
  Regs16 *g = c->regs;
  r.ax = g->ax; r.bx = g->bx; r.cx = g->cx; r.dx = g->dx;
  r.si = g->si; r.di = g->di; r.ds = g->ds; r.es = g->es;
  r.flags = g->flags;
  kernel_dos_strict = harness_is_strict();
  DOS3Call(&r);
  g->ax = r.ax; g->bx = r.bx; g->cx = r.cx; g->dx = r.dx;
  g->si = r.si; g->di = r.di; g->es = r.es;
  g->flags = (uint16_t)((g->flags & ~1u) | (r.flags & 1u));
  c->ret = 0;
  stop_if_fatal();
  if (kernel_exit_pending) harness_abort_run(); /* AH=4Ch: the program is over */
}

static void t_GetPrivateProfileInt(HCall *c) {
  c->ret = GetPrivateProfileInt(w32(c, 5), w32(c, 3), (int16_t)hcall_word(c, 2), w32(c, 0));
}
static void t_GetPrivateProfileString(HCall *c) {
  c->ret = GetPrivateProfileString(w32(c, 9), w32(c, 7), w32(c, 5), w32(c, 3), hcall_word(c, 2),
                                   w32(c, 0));
}
static void t_WritePrivateProfileString(HCall *c) {
  c->ret = WritePrivateProfileString(w32(c, 6), w32(c, 4), w32(c, 2), w32(c, 0));
}

/* After a fatal call that returned (a kernel_fatal_hook is installed) the program is over:
 * abort the whole run, including enclosing nested runs (an API handler that called game
 * code), so the game never sees the return. */
static void stop_if_fatal(void) {
  if (kernel_fatal_pending) harness_abort_run();
}

/* FatalExit(int code) is PASCAL with ONE word (2 argbytes) in the SDK, but its entry in
 * win16_api.inc is a STUB with argbytes 0 (tools/winespec lists it as "stub"). The code is
 * the first stack word either way. The thunk's RETF does not pop the word, which does not
 * matter: the emulation stops here and never returns into game code. */
static void t_FatalExit(HCall *c) {
  FatalExit((int16_t)hcall_word(c, 0));
  stop_if_fatal();
}

/* FatalAppExit(uAction, lpMessageText): 6 argbytes. */
static void t_FatalAppExit(HCall *c) {
  FatalAppExit(hcall_word(c, 2), w32(c, 0));
  stop_if_fatal();
}

static const struct {
  uint16_t ordinal;
  HApiFn fn;
} k_apis[] = {
    {91, t_InitTask},          {30, t_WaitEvent},         {29, t_Yield},
    {3, t_GetVersion},         {49, t_GetModuleFileName}, {51, t_MakeProcInstance},
    {52, t_FreeProcInstance},  {15, t_GlobalAlloc},       {16, t_GlobalReAlloc},
    {17, t_GlobalFree},        {18, t_GlobalLock},        {19, t_GlobalUnlock},
    {20, t_GlobalSize},        {23, t_LockSegment},       {24, t_UnlockSegment},
    {5, t_LocalAlloc},         {6, t_LocalReAlloc},       {7, t_LocalFree},
    {10, t_LocalSize},         {60, t_FindResource},      {61, t_LoadResource},
    {62, t_LockResource},      {63, t_FreeResource},      {65, t_SizeofResource},
    {66, t_AllocResource},     {88, t_lstrcpy},           {89, t_lstrcat},
    {90, t_lstrlen},           {131, t_GetDOSEnvironment}, {136, t_GetDriveType},
    {1, t_FatalExit},          {137, t_FatalAppExit},
    {74, t_OpenFile},          {81, t_lclose},            {82, t_lread},
    {86, t_lwrite},            {64, t_AccessResource},    {102, t_DOS3Call},
    {127, t_GetPrivateProfileInt}, {128, t_GetPrivateProfileString},
    {129, t_WritePrivateProfileString},
};

void kernel_register(void) {
  for (size_t i = 0; i < sizeof k_apis / sizeof k_apis[0]; i++)
    harness_register("KERNEL", k_apis[i].ordinal, k_apis[i].fn);
}
