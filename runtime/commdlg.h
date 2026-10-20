/* COMMDLG (task stars-0z4.14): GetOpenFileName (COMMDLG.1), GetSaveFileName (COMMDLG.2), PrintDlg
 * (COMMDLG.20, a stub that answers FALSE) and CommDlgExtendedError (COMMDLG.26), as plain C
 * functions named and typed like the Win16 API. No harness dependency; the PASCAL stack adapters
 * are in harness/commdlg_thunks.c.
 *
 * The File Open / Save As dialog is drawn by the runtime from a template built in code (no
 * Microsoft template file): dialog manager (runtime/dialog.c, user_dialog_box) + standard controls
 * (EDIT, LISTBOX, COMBOBOX, BUTTON, STATIC) over the virtual file system (runtime/vfs.h). The dialog
 * function is a runtime procedure in the callback segment (USER_CB_FILEDLG).
 *
 * OPENFILENAME layout. The offsets 0x00 lStructSize, 0x04 hwndOwner, 0x08 lpstrFilter,
 * 0x14 nFilterIndex, 0x18 lpstrFile, 0x1C nMaxFile, 0x20 lpstrFileTitle, 0x24 nMaxFileTitle,
 * 0x28 lpstrInitialDir, 0x2C lpstrTitle, 0x30 Flags, 0x34 nFileOffset, 0x36 nFileExtension,
 * 0x38 lpstrDefExt and the size 0x48 are CONFIRMED by the stores of the game's own three call sites
 * (docs/inventory.md section 19). The other six (0x06 hInstance, 0x0C lpstrCustomFilter, 0x10
 * nMaxCustFilter, 0x3C lCustData, 0x40 lpfnHook, 0x44 lpTemplateName) are CONFIRMED from Wine
 * dlls/commdlg.dll16/cdlg16.h lines 38-59 (#pragma pack(1)). PRINTDLG16 (cdlg16.h ~117-141) is
 * 0x34 bytes with Flags at 0x0C. The OFN_* and PD_* values match Wine include/commdlg.h lines
 * 49-64 and 553-555. */
#ifndef STARS_RUNTIME_COMMDLG_H
#define STARS_RUNTIME_COMMDLG_H

#include <stddef.h>
#include <stdint.h>

#include "runtime/mem.h"
#include "runtime/user.h"

#pragma pack(push, 1)
typedef struct OPENFILENAME16 { /* 0x48 bytes */
  uint32_t lStructSize;         /* 0x00 */
  HWND16 hwndOwner;             /* 0x04 */
  HANDLE16 hInstance;           /* 0x06 */
  FARPTR lpstrFilter;           /* 0x08 pairs "text\0pattern\0", ended by an extra NUL */
  FARPTR lpstrCustomFilter;     /* 0x0C */
  uint32_t nMaxCustFilter;      /* 0x10 */
  uint32_t nFilterIndex;        /* 0x14 1-based */
  FARPTR lpstrFile;             /* 0x18 */
  uint32_t nMaxFile;            /* 0x1C */
  FARPTR lpstrFileTitle;        /* 0x20 */
  uint32_t nMaxFileTitle;       /* 0x24 */
  FARPTR lpstrInitialDir;       /* 0x28 */
  FARPTR lpstrTitle;            /* 0x2C */
  uint32_t Flags;               /* 0x30 */
  uint16_t nFileOffset;         /* 0x34 */
  uint16_t nFileExtension;      /* 0x36 */
  FARPTR lpstrDefExt;           /* 0x38 */
  uint32_t lCustData;           /* 0x3C */
  FARPTR lpfnHook;              /* 0x40 */
  FARPTR lpTemplateName;        /* 0x44 */
} OPENFILENAME16;
#pragma pack(pop)

#define OPENFILENAME16_SIZE 0x48

/* OFN_* flags (Windows 3.1 commdlg.h; the values are shared by every Win16 header) */
#define OFN_READONLY 0x00000001UL
#define OFN_OVERWRITEPROMPT 0x00000002UL
#define OFN_HIDEREADONLY 0x00000004UL
#define OFN_NOCHANGEDIR 0x00000008UL
#define OFN_SHOWHELP 0x00000010UL
#define OFN_ENABLEHOOK 0x00000020UL
#define OFN_ENABLETEMPLATE 0x00000040UL
#define OFN_ENABLETEMPLATEHANDLE 0x00000080UL
#define OFN_NOVALIDATE 0x00000100UL
#define OFN_ALLOWMULTISELECT 0x00000200UL
#define OFN_EXTENSIONDIFFERENT 0x00000400UL
#define OFN_PATHMUSTEXIST 0x00000800UL
#define OFN_FILEMUSTEXIST 0x00001000UL
#define OFN_CREATEPROMPT 0x00002000UL
#define OFN_SHAREAWARE 0x00004000UL
#define OFN_NOREADONLYRETURN 0x00008000UL
#define OFN_NOTESTFILECREATE 0x00010000UL

/* CommDlgExtendedError codes (cderr.h) */
#define CDERR_STRUCTSIZE 0x0001
#define CDERR_INITIALIZATION 0x0002
#define CDERR_DIALOGFAILURE 0xFFFF
#define FNERR_BUFFERTOOSMALL 0x3003

/* PRINTDLG: 0x34 bytes in Win16 (the game stores lStructSize = 0x34, Flags at 0x0C = 0x500) */
#define PRINTDLG16_SIZE 0x34
#define PD_RETURNDC 0x0100UL
#define PD_RETURNDEFAULT 0x0400UL

/* Dialog control ids (dlgs.h; CONFIRMED from Wine include/dlgs.h) */
#define CD_chx1 0x0410 /* Read Only check box */
#define CD_stc1 0x0440 /* current directory path */
#define CD_lst1 0x0460 /* file list */
#define CD_lst2 0x0461 /* directory list */
#define CD_cmb1 0x0470 /* List Files of Type */
#define CD_cmb2 0x0471 /* Drives */
#define CD_edt1 0x0480 /* File Name */

/* The dialog function in the callback segment (offset 0x00A0; 0x0090 is the pop-up menu class). */
#define USER_CB_FILEDLG MK_FP(MEM_SEL_CALLBACK, 0x00A0)

/* ---- the Win16 API ---- */

/* COMMDLG.1. TRUE when the user chose a file (lpstrFile, lpstrFileTitle, nFileOffset,
 * nFileExtension, nFilterIndex and Flags are filled), FALSE on Cancel or an error (then
 * CommDlgExtendedError tells which). */
uint16_t GetOpenFileName(FARPTR lpofn);
/* COMMDLG.2. As GetOpenFileName with the Save As title and OFN_OVERWRITEPROMPT /
 * OFN_NOREADONLYRETURN checks. */
uint16_t GetSaveFileName(FARPTR lpofn);
/* COMMDLG.20. Printing is a separate task (stars-0z4.16): answers FALSE, as if the user cancelled,
 * and sets the extended error to 0. */
uint16_t PrintDlg(FARPTR lppd);
/* COMMDLG.26. The error of the latest COMMDLG call (0 after a Cancel or a success). */
uint32_t CommDlgExtendedError(void);

/* ---- runtime internals ---- */

uint32_t user_filedlg_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
/* Frees the COMMDLG state (called by user_shutdown). */
void commdlg_shutdown(void);
/* Test hook: the dialog window of the file dialog being shown (0 when none). */
HWND16 commdlg_active_dialog(void);

#endif /* STARS_RUNTIME_COMMDLG_H */
