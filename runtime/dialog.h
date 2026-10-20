/* Dialog manager (task stars-0z4.12): DialogBox, CreateDialog, EndDialog, GetDlgItem,
 * SetDlgItemText, GetDlgItemText, SendDlgItemMessage, DefDlgProc and the dialog keyboard
 * interface (IsDialogMessage), as plain C functions named and typed like the Win16 API. No
 * harness dependency; the PASCAL stack adapters are in harness/user_thunks.c.
 *
 * Model. A dialog is a window of the runtime class "#32770" whose window procedure is the runtime
 * DefDlgProc (USER_CB_DIALOG in the callback segment, so GetWindowLong(GWL_WNDPROC) and
 * CallWindowProc work). The 30 bytes of window extra memory (DLGWINDOWEXTRA, in the arena) are
 * DWL_MSGRESULT (0), DWL_DLGPROC (4), DWL_USER (8) and then the manager's private state, so a
 * snapshot of the arena covers every dialog. Every message goes to DefDlgProc, which offers it to
 * the application's dialog function first (a far pointer, original code or decompiled C): a
 * nonzero answer is the message result (DWL_MSGRESULT, except WM_INITDIALOG / WM_CTLCOLOR and the
 * other messages whose answer is the BOOL or HBRUSH itself); a zero answer runs the default
 * processing. The dialog function returns a BOOL in AX only, so the answer is masked to 16 bits.
 *
 * Templates are the Windows 3.x DLGTEMPLATE read by ne_parse_dialog (runtime/ne.h) from the
 * RT_DIALOG resource of the loaded module. Predefined item classes 0x80..0x85 are BUTTON, EDIT,
 * STATIC, LISTBOX, SCROLLBAR and COMBOBOX; a class named in a template must already be registered
 * (docs/inventory.md section 17 lists the classes the game's 36 templates use: none custom).
 *
 * Every Windows 3.1 rule below that no SDK text or capture backs is marked UNVERIFIED at the code
 * that implements it and in docs/inventory.md section 17; Wine (wine-mirror/wine commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0, dlls/user.exe16/dialog.c and dlls/user32/dialog.c)
 * is the behavioural reference. */
#ifndef STARS_RUNTIME_DIALOG_H
#define STARS_RUNTIME_DIALOG_H

#include <stddef.h>

#include "runtime/user.h"

/* Messages (Win16 numbering: winuser.rh 342 / 406 / 411 and winuser16.h DM_*, UNVERIFIED for 3.1 by
 * a capture but shared by every Win16 header). */
#define WM_NEXTDLGCTL 0x0028
#define WM_INITDIALOG 0x0110
#define WM_VKEYTOITEM 0x002E
#define WM_CHARTOITEM 0x002F
#define WM_QUERYDRAGICON 0x0037
#define DM_GETDEFID (WM_USER + 0)
#define DM_SETDEFID (WM_USER + 1)
#define DC_HASDEFID 0x534B
#define DLGC_WANTMESSAGE 0x0004 /* same bit as DLGC_WANTALLKEYS */

/* Window extra memory of a dialog window */
#define DWL_MSGRESULT 0
#define DWL_DLGPROC 4
#define DWL_USER 8
#define DLGWINDOWEXTRA 30

/* Dialog styles (low word of the template style) */
#define DS_ABSALIGN 0x0001
#define DS_SYSMODAL 0x0002
#define DS_LOCALEDIT 0x0020
#define DS_SETFONT 0x0040
/* DS_MODALFRAME 0x0080 is in user.h */
#define DS_NOIDLEMSG 0x0100

/* The runtime dialog class procedure (callback segment). */
#define USER_CB_DIALOG MK_FP(MEM_SEL_CALLBACK, 0x0080)

/* ---- the Win16 API ---- */

/* USER.87. Creates the dialog from the RT_DIALOG resource lpTemplateName of hInstance, disables the
 * owner, runs a modal message loop and returns the value passed to EndDialog; -1 when the template
 * or the window cannot be created; 0 when the message loop ends without EndDialog (WM_QUIT is
 * posted again, or no more input in a native test run). The dialog is shown whether or not the
 * template has WS_VISIBLE. */
int16_t DialogBox(HANDLE16 hInstance, FARPTR lpTemplateName, HWND16 hWndParent, FARPTR lpDialogFunc);
/* USER.89. Modeless: returns the dialog window (0 on failure); shown only when the template has
 * WS_VISIBLE. */
HWND16 CreateDialog(HANDLE16 hInstance, FARPTR lpTemplateName, HWND16 hWndParent, FARPTR lpDialogFunc);
/* USER.88. Ends a dialog: records the result, enables the owner again and hides the dialog. A
 * modal dialog's DialogBox returns nResult. Returns 1, or 0 when hDlg is not a dialog (or is
 * already ended). */
uint16_t EndDialog(HWND16 hDlg, int16_t nResult);
/* USER.91. The direct child with that id (the first one in z order when several share it); 0 when
 * none. Not recursive. */
HWND16 GetDlgItem(HWND16 hDlg, int16_t nIDDlgItem);
/* USER.92 / USER.93. GetDlgItemText returns the copied length; for a missing control it stores an
 * empty string and returns 0. */
void SetDlgItemText(HWND16 hDlg, int16_t nIDDlgItem, FARPTR lpString);
int16_t GetDlgItemText(HWND16 hDlg, int16_t nIDDlgItem, FARPTR lpString, int16_t nMaxCount);
/* USER.101. SendMessage to the control; 0 when it does not exist. */
uint32_t SendDlgItemMessage(HWND16 hDlg, int16_t nIDDlgItem, uint16_t msg, uint16_t wParam, uint32_t lParam);
/* USER.308 (not imported by the game). */
uint32_t DefDlgProc(HWND16 hDlg, uint16_t msg, uint16_t wParam, uint32_t lParam);
/* USER.90 (NOT imported by the game, so modeless dialogs of the game get no dialog navigation; the
 * modal loop of DialogBox uses it). For a keyboard message addressed to hDlg or a descendant:
 * Tab / Shift+Tab (WM_NEXTDLGCTL), arrow keys (next/previous control of the WS_GROUP group), Enter
 * (the focused push button, else the default push button, else IDOK), Escape (IDCANCEL) and Alt+
 * mnemonic letters; every other key message is translated and dispatched here. Returns 1 when the
 * message was for the dialog (nothing more to do), 0 otherwise. */
uint16_t IsDialogMessage(HWND16 hDlg, FARPTR lpMsg);

/* ---- runtime internals ---- */

/* Registers the class "#32770"; called by user_init. */
void user_dlg_init(void);
uint32_t user_dialog_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);

/* The same as CreateDialog / DialogBox for a template held in host memory (DialogBoxIndirect /
 * CreateDialogIndirectParam without the memory handle); lParam is the WM_INITDIALOG lParam. */
HWND16 user_dialog_create(const uint8_t *tmpl, size_t len, HWND16 hWndParent, FARPTR lpDialogFunc, uint32_t lParam);
int16_t user_dialog_box(const uint8_t *tmpl, size_t len, HWND16 hWndParent, FARPTR lpDialogFunc, uint32_t lParam);

/* Dialog base units used to convert the template's dialog units to pixels (x/4, y/8); false when
 * hDlg is not a dialog. */
bool user_dialog_units(HWND16 hDlg, int *xbase, int *ybase);
/* DM_GETDEFID / tab order helpers, for tests and the host: next WS_TABSTOP control after cur (0 =
 * start), forward or backward, skipping hidden and disabled ones; 0 when there is none. */
HWND16 user_dlg_next_tab(HWND16 hDlg, HWND16 cur, bool prev);

#endif /* STARS_RUNTIME_DIALOG_H */
