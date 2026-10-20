/* Win16 types and constants shared by the runtime API files (KERNEL, USER, GDI ...).
 * Values are the Windows 3.1 ones. Add constants here as tasks need them. */
#ifndef STARS_RUNTIME_INCLUDE_WIN16_H
#define STARS_RUNTIME_INCLUDE_WIN16_H

#include <stdint.h>

typedef uint16_t HANDLE16; /* HGLOBAL, HLOCAL, HRSRC, HINSTANCE, HTASK ... */
typedef uint16_t WORD16;
typedef uint32_t DWORD16;
typedef uint16_t UINT16;
typedef int16_t INT16;
typedef int16_t HFILE16; /* file handle; HFILE_ERROR on failure */
typedef uint32_t FARPTR; /* selector:offset, see runtime/mem.h MK_FP */

/* Resource types (MAKEINTRESOURCE values). */
#define RT_CURSOR 1
#define RT_BITMAP 2
#define RT_ICON 3
#define RT_MENU 4
#define RT_DIALOG 5
#define RT_STRING 6
#define RT_FONTDIR 7
#define RT_FONT 8
#define RT_ACCELERATOR 9
#define RT_RCDATA 10

/* LocalAlloc / LocalReAlloc flags that runtime/mem.c does not implement (Windows values).
 * The KERNEL wrappers strip the ones that are no-ops for a fixed heap. */
#define LMEM_MOVEABLE 0x0002
#define LMEM_NOCOMPACT 0x0010
#define LMEM_NODISCARD 0x0020
#define LMEM_DISCARDABLE 0x0F00

/* OpenFile (KERNEL.74) flags and OFSTRUCT layout: cBytes(1) fFixedDisk(1) nErrCode(2)
 * reserved(4) szPathName(128) = 136 bytes. */
#define HFILE_ERROR ((HFILE16)-1)
#define OF_READ 0x0000
#define OF_WRITE 0x0001
#define OF_READWRITE 0x0002
#define OF_SHARE_COMPAT 0x0000
#define OF_SHARE_EXCLUSIVE 0x0010
#define OF_SHARE_DENY_WRITE 0x0020
#define OF_SHARE_DENY_READ 0x0030
#define OF_SHARE_DENY_NONE 0x0040
#define OF_PARSE 0x0100
#define OF_DELETE 0x0200
#define OF_VERIFY 0x0400
#define OF_CANCEL 0x0800
#define OF_CREATE 0x1000
#define OF_PROMPT 0x2000
#define OF_EXIST 0x4000
#define OF_REOPEN 0x8000
#define OFSTRUCT_SIZE 136
#define OFS_CBYTES 0
#define OFS_FFIXEDDISK 1
#define OFS_NERRCODE 2
#define OFS_PATHNAME 8
#define OFS_PATHNAME_LEN 128

/* GetDriveType results. */
#define DRIVE_UNKNOWN 0
#define DRIVE_REMOVABLE 2
#define DRIVE_FIXED 3
#define DRIVE_REMOTE 4

/* GetVersion: Windows 3.10 in the low word (AL = major, AH = minor). */
#define W16_WINVER 0x0A03u

#endif /* STARS_RUNTIME_INCLUDE_WIN16_H */
