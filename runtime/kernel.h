/* KERNEL part 1 (task stars-0z4.2): start-up, global/local memory, resources, module,
 * strings and misc, as plain C functions named and typed like the Win16 API. This file and
 * runtime/kernel.c do not depend on the harness, so the browser build can use them as they
 * are. The native harness decodes the PASCAL stack into these calls in
 * harness/kernel_thunks.c (kernel_register). Far pointers are FARPTR (selector:offset). */
#ifndef STARS_RUNTIME_KERNEL_H
#define STARS_RUNTIME_KERNEL_H

#include <stddef.h>
#include <stdint.h>

#include "runtime/include/win16.h"

/* Parses the NE image (copied; the caller may free it), takes hInstance = the auto data
 * segment selector, and allocates the PSP and the DOS environment block in the arena.
 * Call after mem_init (harness_init). Files, clock, DTA and vectors already in the VFS are
 * kept; only C:\STARS\STARS26I.EXE is (re)written. Returns 0 on success, negative on failure. */
int kernel_load(const uint8_t *image, size_t len);
/* Releases the host-side state (image copy, NE tables) and the resource-state block in the
 * runtime region. Call it while the arena is alive, i.e. BEFORE harness_shutdown /
 * mem_shutdown. The PSP, environment and loaded-resource blocks are global-heap blocks;
 * the arena teardown reclaims them, so they are not freed one by one here. */
void kernel_shutdown(void);

/* FatalExit / FatalAppExit message sink. NULL (default): print to stderr and abort().
 * When set, it is called and then kernel_fatal_pending is set: the API must not return
 * into game code, so the host adapter aborts the whole run (harness_abort_run, which also
 * ends enclosing nested runs, not just the innermost one). Reset by kernel_shutdown. */
extern void (*kernel_fatal_hook)(const char *msg);
extern int kernel_fatal_pending;

/* DOS terminate (INT 21h AH=4Ch, runtime/kernel_file.c): sets kernel_exit_pending and the
 * return code; the host adapter then ends the run. Reset by kernel_shutdown. */
extern int kernel_exit_pending;
extern int kernel_exit_code;
/* Reports a fatal condition through kernel_fatal_hook (or prints and aborts), then sets
 * kernel_fatal_pending. */
void kernel_fatal(const char *msg);

/* DOS version of the reference environment (GetVersion high word and INT 21h AH=30h). */
#define KERNEL_DOS_VERSION_WORD 0x0616u

/* Selector of the PSP and of the DOS environment block (valid after kernel_load). */
uint16_t kernel_psp_selector(void);

/* ---- start-up ---- */

/* InitTask is register-based; the result registers: AX=1, BX = command-line text (0x81, or
 * 0x80 when the tail is empty so that ES:BX reads as an empty C string, as Wine's
 * InitTask16 does), CX = stack size, DX = nCmdShow (SW_SHOWNORMAL), SI = hPrevInstance,
 * DI = hInstance, ES = PSP selector. */
typedef struct KInitTaskRegs {
  uint16_t ax, bx, cx, dx, si, di, es;
} KInitTaskRegs;
void InitTask(KInitTaskRegs *out);
uint16_t WaitEvent(uint16_t hTask); /* returns 0 (Wine: FALSE when not waited); the game ignores it */
void Yield(void);
uint32_t GetVersion(void); /* DOS version in the high word, Windows in the low word */

/* ---- module ---- */

/* hModule 0 means the current task. Returns the length copied, 0 on failure (unknown module,
 * or the nSize bytes at lpFilename are not all addressable). */
uint16_t GetModuleFileName(HANDLE16 hModule, FARPTR lpFilename, uint16_t nSize);
FARPTR MakeProcInstance(FARPTR lpProc, HANDLE16 hInstance);
void FreeProcInstance(FARPTR lpProc);

/* ---- global/local memory ----
 * Global* are the runtime/mem.h functions. LocalAlloc/LocalReAlloc in mem.h support only
 * LMEM_FIXED/LMEM_ZEROINIT; these KERNEL-level wrappers first drop the flags that are
 * no-ops for a fixed heap (LMEM_NOCOMPACT, LMEM_NODISCARD, and LMEM_MOVEABLE for
 * LocalReAlloc, which mem.c already allows) and still abort on real discardable memory. */
uint16_t kernel_LocalAlloc(uint16_t flags, uint16_t bytes);
uint16_t kernel_LocalReAlloc(uint16_t hMem, uint16_t bytes, uint16_t flags);

/* LockSegment/UnlockSegment: seg 0xFFFF means the caller's data segment, passed as ds. A
 * selector that is not a global block (DGROUP, code) is fixed already. */
HANDLE16 LockSegment(uint16_t seg, uint16_t ds);
uint16_t UnlockSegment(uint16_t seg, uint16_t ds); /* nonzero while still locked */

/* ---- resources ---- */

HANDLE16 FindResource(HANDLE16 hInstance, FARPTR lpName, FARPTR lpType);
HANDLE16 LoadResource(HANDLE16 hInstance, HANDLE16 hResInfo);
FARPTR LockResource(HANDLE16 hResData);
uint16_t FreeResource(HANDLE16 hResData); /* 0 on success */
uint32_t SizeofResource(HANDLE16 hInstance, HANDLE16 hResInfo);
HANDLE16 AllocResource(HANDLE16 hInstance, HANDLE16 hResInfo, uint32_t dwSize);

/* AccessResource: a DOS handle on the module's .exe in the VFS (kernel_load puts the image
 * there at the path GetModuleFileName returns), positioned at the resource. HFILE_ERROR on
 * failure. The caller closes it with _lclose. */
HFILE16 AccessResource(HANDLE16 hInstance, HANDLE16 hResInfo);

/* ---- strings (far pointers) ---- */

uint16_t lstrlen(FARPTR lpString);
FARPTR lstrcpy(FARPTR lpDst, FARPTR lpSrc);
FARPTR lstrcat(FARPTR lpDst, FARPTR lpSrc);

/* ---- misc ---- */

FARPTR GetDOSEnvironment(void);
uint16_t GetDriveType(uint16_t nDrive);
/* Both call kernel_fatal_hook (or print and abort), then set kernel_fatal_pending. */
void FatalExit(int16_t code);
void FatalAppExit(uint16_t uAction, FARPTR lpMessageText);

#endif /* STARS_RUNTIME_KERNEL_H */
