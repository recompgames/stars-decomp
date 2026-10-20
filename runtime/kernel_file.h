/* KERNEL part 2 (task stars-0z4.3): file I/O, DOS3Call and the private-profile functions,
 * as plain C functions named and typed like the Win16 API, on top of runtime/vfs.h. No
 * harness dependency: the PASCAL stack / register decoding lives in
 * harness/kernel_thunks.c (kernel_register). Far pointers are FARPTR. */
#ifndef STARS_RUNTIME_KERNEL_FILE_H
#define STARS_RUNTIME_KERNEL_FILE_H

#include <stdint.h>

#include "runtime/include/win16.h"

/* OpenFile fills the OFSTRUCT at lpReopenBuff (see win16.h OFS_*). Returns the handle,
 * HFILE_ERROR on failure (nErrCode = DOS error), 0 for OF_PARSE and 1 for OF_EXIST and
 * OF_DELETE on success. */
HFILE16 OpenFile(FARPTR lpFileName, FARPTR lpReopenBuff, uint16_t uStyle);
HFILE16 _lclose(HFILE16 hFile); /* 0, or HFILE_ERROR */
uint16_t _lread(HFILE16 hFile, FARPTR lpBuffer, uint16_t uBytes);  /* bytes read, or 0xFFFF */
uint16_t _lwrite(HFILE16 hFile, FARPTR lpBuffer, uint16_t uBytes); /* bytes written, or 0xFFFF */

/* DOS3Call (KERNEL.102): the INT 21h register set. flags bit 0 is CF. Implements exactly the
 * AH values the game's C runtime uses (docs/inventory.md section 5) plus 2Ch: 1Ah 25h 2Ah 2Ch
 * 2Fh 30h 35h 36h 39h 3Ah 3Bh 40h 41h 42h 43h 4Ch 4Eh 4Fh 56h. Any other AH value aborts
 * (kernel_fatal) when kernel_dos_strict is set, else returns CF=1, AX=1. */
typedef struct KDosRegs {
  uint16_t ax, bx, cx, dx, si, di, ds, es, flags;
} KDosRegs;
void DOS3Call(KDosRegs *r);
extern int kernel_dos_strict;

/* Private profile files: a name without a path refers to C:\WINDOWS. */
uint16_t GetPrivateProfileInt(FARPTR lpApp, FARPTR lpKey, int16_t nDefault, FARPTR lpFile);
uint16_t GetPrivateProfileString(FARPTR lpApp, FARPTR lpKey, FARPTR lpDefault, FARPTR lpRet,
                                 uint16_t nSize, FARPTR lpFile);
uint16_t WritePrivateProfileString(FARPTR lpApp, FARPTR lpKey, FARPTR lpString, FARPTR lpFile);

#endif /* STARS_RUNTIME_KERNEL_FILE_H */
