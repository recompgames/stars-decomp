/* HCall decoders for the COMMDLG functions of runtime/commdlg.h. Native builds only. Each of
 * GetOpenFileName, GetSaveFileName and PrintDlg takes one far pointer (high word pushed first, so
 * its low word is word 0) and returns a BOOL in AX (RET16). CommDlgExtendedError takes nothing and
 * returns a DWORD in DX:AX, which the dispatcher takes from c->ret (low 16 bits in AX, high 16 in
 * DX as for the other DWORD results such as GetWindowLong). */
#include "harness/commdlg_thunks.h"

#include "harness/harness.h"
#include "runtime/commdlg.h"

/* See harness/byname.h: a scenario's answer for PrintDlg in the by-name calls of decompiled code (NULL: the runtime). */
uint16_t (*byname_printdlg_hook)(uint32_t lppd) = NULL;

static uint32_t w32(const HCall *c, unsigned n) {
  return (uint32_t)hcall_word(c, n) | ((uint32_t)hcall_word(c, n + 1) << 16);
}

static void t_GetOpenFileName(HCall *c) { c->ret = GetOpenFileName(w32(c, 0)); }
static void t_GetSaveFileName(HCall *c) { c->ret = GetSaveFileName(w32(c, 0)); }
static void t_PrintDlg(HCall *c) { c->ret = PrintDlg(w32(c, 0)); }
static void t_CommDlgExtendedError(HCall *c) { c->ret = CommDlgExtendedError(); }

void commdlg_register(void) {
  harness_register("COMMDLG", 1, t_GetOpenFileName);
  harness_register("COMMDLG", 2, t_GetSaveFileName);
  harness_register("COMMDLG", 20, t_PrintDlg);
  harness_register("COMMDLG", 26, t_CommDlgExtendedError);
}
