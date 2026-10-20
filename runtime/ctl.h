/* Standard controls, part 1 (task stars-0z4.10): the BUTTON, STATIC and SCROLLBAR window classes
 * and the dialog button helpers CheckDlgButton, CheckRadioButton and IsDlgButtonChecked.
 *
 * Model: each class is a runtime window procedure exposed as a far pointer in the runtime callback
 * segment (MEM_SEL_CALLBACK, offsets USER_CB_BUTTON / USER_CB_STATIC / USER_CB_SCROLLBAR), so that
 * GetWindowLong(GWL_WNDPROC) hands it to a subclass and CallWindowProc forwards to it. The classes
 * are registered by user_init (ctl.c). Per-window control state lives in the window's extra bytes
 * (cbWndExtra), which CreateWindow allocates in the arena, and the scroll bar position and range in
 * the UserState scroll table (SB_CTL), so a snapshot of the arena covers all of it.
 *
 * Message numbers are the Windows 3.1 ones (Wine include/wine/winuser16.h at commit
 * 5b6b85421c2a2161728b2a55e9f09271770a3eb0; the lines are cited next to each group). Where that
 * header has no block (BS_/SS_/SBS_ styles, BN_/STN_ codes, DLGC_/ODS_ values, ESB_ flags) the
 * value is taken from include/winuser.h / winuser.rh at the same commit and is UNVERIFIED for
 * Windows 3.1 (the low values are shared by Win16 and Win32). Behaviour follows Wine's controls
 * (dlls/user32/button.c, static.c, dlls/win32u/scroll.c), also UNVERIFIED for Windows 3.1; the
 * list of choices is in docs/inventory.md section 15, "Standard controls part 1". Plain C, no
 * harness dependency. */
#ifndef STARS_RUNTIME_CTL_H
#define STARS_RUNTIME_CTL_H

#include "runtime/user.h"

/* ---- window messages used by the controls (Win16 numbering) ---- */
#define WM_DRAWITEM 0x002B   /* winuser.rh 347 */
#define WM_SETFONT 0x0030    /* winuser.rh 352 */
#define WM_GETFONT 0x0031    /* winuser.rh 353 */
#define WM_GETDLGCODE 0x0087 /* winuser.rh 416 */
#define WM_COMMAND 0x0111    /* Win16: wParam = id, LOWORD(lParam) = control, HIWORD(lParam) = code */
#define WM_SYSCOLORCHANGE 0x0015

#define WS_TABSTOP 0x00010000UL
#define WS_GROUP 0x00020000UL

/* WM_CTLCOLOR types: HIWORD(lParam); winuser.h 930-937 */
#define CTLCOLOR_MSGBOX 0
#define CTLCOLOR_EDIT 1
#define CTLCOLOR_LISTBOX 2
#define CTLCOLOR_BTN 3
#define CTLCOLOR_DLG 4
#define CTLCOLOR_SCROLLBAR 5
#define CTLCOLOR_STATIC 6

/* WM_GETDLGCODE results: winuser.h 1101-1111 */
#define DLGC_WANTARROWS 0x0001
#define DLGC_WANTTAB 0x0002
#define DLGC_WANTALLKEYS 0x0004
#define DLGC_HASSETSEL 0x0008
#define DLGC_DEFPUSHBUTTON 0x0010
#define DLGC_UNDEFPUSHBUTTON 0x0020
#define DLGC_RADIOBUTTON 0x0040
#define DLGC_WANTCHARS 0x0080
#define DLGC_STATIC 0x0100
#define DLGC_BUTTON 0x2000

/* ---- BUTTON ---- */
#define BM_GETCHECK (WM_USER + 0) /* winuser16.h 444-448 */
#define BM_SETCHECK (WM_USER + 1)
#define BM_GETSTATE (WM_USER + 2)
#define BM_SETSTATE (WM_USER + 3)
#define BM_SETSTYLE (WM_USER + 4)

#define BS_PUSHBUTTON 0x00
#define BS_DEFPUSHBUTTON 0x01
#define BS_CHECKBOX 0x02
#define BS_AUTOCHECKBOX 0x03
#define BS_RADIOBUTTON 0x04
#define BS_3STATE 0x05
#define BS_AUTO3STATE 0x06
#define BS_GROUPBOX 0x07
#define BS_USERBUTTON 0x08
#define BS_AUTORADIOBUTTON 0x09
#define BS_OWNERDRAW 0x0B
#define BS_TYPEMASK 0x0F
#define BS_LEFTTEXT 0x20

#define BN_CLICKED 0
#define BN_PAINT 1
#define BN_HILITE 2
#define BN_UNHILITE 3
#define BN_DISABLE 4
#define BN_DOUBLECLICKED 5

/* Owner-draw (WM_DRAWITEM): winuser16.h 487-498, winuser.h 865-884 */
#define ODT_BUTTON 4
#define ODA_DRAWENTIRE 0x0001
#define ODA_SELECT 0x0002
#define ODA_FOCUS 0x0004
#define ODS_SELECTED 0x0001
#define ODS_GRAYED 0x0002
#define ODS_DISABLED 0x0004
#define ODS_CHECKED 0x0008
#define ODS_FOCUS 0x0010

#pragma pack(push, 1)
typedef struct DRAWITEMSTRUCT16 { /* 26 bytes */
  uint16_t CtlType, CtlID, itemID, itemAction, itemState;
  HWND16 hwndItem;
  HDC16 hDC;
  RECT16 rcItem;
  uint32_t itemData;
} DRAWITEMSTRUCT16;
#pragma pack(pop)

/* ---- STATIC ---- */
#define STM_SETICON (WM_USER + 0) /* winuser16.h 451-452 */
#define STM_GETICON (WM_USER + 1)

#define SS_LEFT 0x00
#define SS_CENTER 0x01
#define SS_RIGHT 0x02
#define SS_ICON 0x03
#define SS_BLACKRECT 0x04
#define SS_GRAYRECT 0x05
#define SS_WHITERECT 0x06
#define SS_BLACKFRAME 0x07
#define SS_GRAYFRAME 0x08
#define SS_WHITEFRAME 0x09
#define SS_USERITEM 0x0A
#define SS_SIMPLE 0x0B
#define SS_LEFTNOWORDWRAP 0x0C
#define SS_TYPEMASK 0x1F
#define SS_NOPREFIX 0x80
#define SS_NOTIFY 0x100

#define STN_CLICKED 0 /* winuser.h 1831-1834 */
#define STN_DBLCLK 1
#define STN_ENABLE 2
#define STN_DISABLE 3

/* ---- SCROLLBAR ---- */
#define SBM_SETPOS (WM_USER + 0) /* winuser16.h 399-403 */
#define SBM_GETPOS (WM_USER + 1)
#define SBM_SETRANGE (WM_USER + 2)
#define SBM_GETRANGE (WM_USER + 3)
#define SBM_ENABLE_ARROWS (WM_USER + 4)

#define SBS_HORZ 0x0000
#define SBS_VERT 0x0001

#define ESB_ENABLE_BOTH 0x0000 /* winuser.h 1810-1822 (UNVERIFIED for Win16) */
#define ESB_DISABLE_LTUP 0x0001
#define ESB_DISABLE_RTDN 0x0002
#define ESB_DISABLE_BOTH 0x0003

/* ======== Standard controls, part 2 (task stars-0z4.11): EDIT, LISTBOX, COMBOBOX ========
 *
 * Message numbers: Wine include/wine/winuser16.h at commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0
 * (EM_* lines 455-485, LB_* lines 195-231, CB_* lines 234-258, MEASUREITEMSTRUCT16 / DELETEITEMSTRUCT16 /
 * COMPAREITEMSTRUCT16 lines 500-528). Styles and notification codes are not in that header; they are
 * the include/winuser.h / winuser.rh values at the same commit (ES_ rh 1134-1148, LBS_ rh 1320-1336,
 * CBS_ rh 1341-1350, EN_ h 957-964, LBN_ h 1929-1934, CBN_ h 1983-1990, DDL_ h 3037-3046) and are
 * UNVERIFIED for Windows 3.1 (the low values are shared by Win16 and Win32). CBN_CLOSEUP, CBN_SELENDOK and
 * CBN_SELENDCANCEL are Win32 and are never sent. */

#define WM_MEASUREITEM 0x002C /* winuser.rh 348 */
#define WM_DELETEITEM 0x002D  /* winuser.rh 349 */
#define WM_COMPAREITEM 0x0039 /* winuser.rh 361 */
#define WM_CUT 0x0300         /* winuser.rh 633-637 */
#define WM_COPY 0x0301
#define WM_PASTE 0x0302
#define WM_CLEAR 0x0303
#define WM_UNDO 0x0304

#ifndef VK_PRIOR
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_F4 0x73
#endif

#define ODT_LISTBOX 2
#define ODT_COMBOBOX 3
#define ODS_COMBOBOXEDIT 0x1000

#pragma pack(push, 1)
typedef struct MEASUREITEMSTRUCT16 { /* 14 bytes; winuser16.h 500-508 */
  uint16_t CtlType, CtlID, itemID, itemWidth, itemHeight;
  uint32_t itemData;
} MEASUREITEMSTRUCT16;
typedef struct DELETEITEMSTRUCT16 { /* 12 bytes; winuser16.h 510-517 */
  uint16_t CtlType, CtlID, itemID;
  HWND16 hwndItem;
  uint32_t itemData;
} DELETEITEMSTRUCT16;
typedef struct COMPAREITEMSTRUCT16 { /* 18 bytes; winuser16.h 519-528 */
  uint16_t CtlType, CtlID;
  HWND16 hwndItem;
  uint16_t itemID1;
  uint32_t itemData1;
  uint16_t itemID2;
  uint32_t itemData2;
} COMPAREITEMSTRUCT16;
#pragma pack(pop)

/* ---- EDIT ---- */
#define EM_GETSEL (WM_USER + 0) /* winuser16.h 455-485 */
#define EM_SETSEL (WM_USER + 1)
#define EM_GETRECT (WM_USER + 2)
#define EM_SETRECT (WM_USER + 3)
#define EM_SETRECTNP (WM_USER + 4)
#define EM_SCROLL (WM_USER + 5)
#define EM_LINESCROLL (WM_USER + 6)
#define EM_SCROLLCARET (WM_USER + 7)
#define EM_GETMODIFY (WM_USER + 8)
#define EM_SETMODIFY (WM_USER + 9)
#define EM_GETLINECOUNT (WM_USER + 10)
#define EM_LINEINDEX (WM_USER + 11)
#define EM_SETHANDLE (WM_USER + 12)
#define EM_GETHANDLE (WM_USER + 13)
#define EM_GETTHUMB (WM_USER + 14)
#define EM_LINELENGTH (WM_USER + 17)
#define EM_REPLACESEL (WM_USER + 18)
#define EM_GETLINE (WM_USER + 20)
#define EM_LIMITTEXT (WM_USER + 21)
#define EM_CANUNDO (WM_USER + 22)
#define EM_UNDO (WM_USER + 23)
#define EM_FMTLINES (WM_USER + 24)
#define EM_LINEFROMCHAR (WM_USER + 25)
#define EM_SETTABSTOPS (WM_USER + 27)
#define EM_SETPASSWORDCHAR (WM_USER + 28)
#define EM_EMPTYUNDOBUFFER (WM_USER + 29)
#define EM_GETFIRSTVISIBLELINE (WM_USER + 30)
#define EM_SETREADONLY (WM_USER + 31)
#define EM_SETWORDBREAKPROC (WM_USER + 32)
#define EM_GETWORDBREAKPROC (WM_USER + 33)
#define EM_GETPASSWORDCHAR (WM_USER + 34)

#define ES_LEFT 0x0000UL
#define ES_CENTER 0x0001UL
#define ES_RIGHT 0x0002UL
#define ES_MULTILINE 0x0004UL
#define ES_UPPERCASE 0x0008UL
#define ES_LOWERCASE 0x0010UL
#define ES_PASSWORD 0x0020UL
#define ES_AUTOVSCROLL 0x0040UL
#define ES_AUTOHSCROLL 0x0080UL
#define ES_NOHIDESEL 0x0100UL
#define ES_OEMCONVERT 0x0400UL
#define ES_READONLY 0x0800UL

#define EN_SETFOCUS 0x0100
#define EN_KILLFOCUS 0x0200
#define EN_CHANGE 0x0300
#define EN_UPDATE 0x0400
#define EN_ERRSPACE 0x0500
#define EN_MAXTEXT 0x0501
#define EN_HSCROLL 0x0601
#define EN_VSCROLL 0x0602

/* ---- LISTBOX ---- */
#define LB_ADDSTRING (WM_USER + 1) /* winuser16.h 195-231 */
#define LB_INSERTSTRING (WM_USER + 2)
#define LB_DELETESTRING (WM_USER + 3)
#define LB_SELITEMRANGEEX (WM_USER + 4)
#define LB_RESETCONTENT (WM_USER + 5)
#define LB_SETSEL (WM_USER + 6)
#define LB_SETCURSEL (WM_USER + 7)
#define LB_GETSEL (WM_USER + 8)
#define LB_GETCURSEL (WM_USER + 9)
#define LB_GETTEXT (WM_USER + 10)
#define LB_GETTEXTLEN (WM_USER + 11)
#define LB_GETCOUNT (WM_USER + 12)
#define LB_SELECTSTRING (WM_USER + 13)
#define LB_DIR (WM_USER + 14)
#define LB_GETTOPINDEX (WM_USER + 15)
#define LB_FINDSTRING (WM_USER + 16)
#define LB_GETSELCOUNT (WM_USER + 17)
#define LB_GETSELITEMS (WM_USER + 18)
#define LB_SETTABSTOPS (WM_USER + 19)
#define LB_GETHORIZONTALEXTENT (WM_USER + 20)
#define LB_SETHORIZONTALEXTENT (WM_USER + 21)
#define LB_SETCOLUMNWIDTH (WM_USER + 22)
#define LB_ADDFILE (WM_USER + 23)
#define LB_SETTOPINDEX (WM_USER + 24)
#define LB_GETITEMRECT (WM_USER + 25)
#define LB_GETITEMDATA (WM_USER + 26)
#define LB_SETITEMDATA (WM_USER + 27)
#define LB_SELITEMRANGE (WM_USER + 28)
#define LB_SETANCHORINDEX (WM_USER + 29)
#define LB_GETANCHORINDEX (WM_USER + 30)
#define LB_SETCARETINDEX (WM_USER + 31)
#define LB_GETCARETINDEX (WM_USER + 32)
#define LB_SETITEMHEIGHT (WM_USER + 33)
#define LB_GETITEMHEIGHT (WM_USER + 34)
#define LB_FINDSTRINGEXACT (WM_USER + 35)
#define LB_CARETON (WM_USER + 36)
#define LB_CARETOFF (WM_USER + 37)

#define LBS_NOTIFY 0x0001UL
#define LBS_SORT 0x0002UL
#define LBS_NOREDRAW 0x0004UL
#define LBS_MULTIPLESEL 0x0008UL
#define LBS_OWNERDRAWFIXED 0x0010UL
#define LBS_OWNERDRAWVARIABLE 0x0020UL
#define LBS_HASSTRINGS 0x0040UL
#define LBS_USETABSTOPS 0x0080UL
#define LBS_NOINTEGRALHEIGHT 0x0100UL
#define LBS_MULTICOLUMN 0x0200UL
#define LBS_WANTKEYBOARDINPUT 0x0400UL
#define LBS_DISABLENOSCROLL 0x1000UL /* keep the vertical scroll bar, disabled, when the list fits (winuser.h; UNVERIFIED for Win16) */
#define LBS_EXTENDEDSEL 0x0800UL
#define LBS_COMBOBOX 0x8000UL /* the list belongs to a combo box; CREATESTRUCT.lpCreateParams is the combo box handle (winuser.rh 1335) */
#define LBS_STANDARD (LBS_NOTIFY | LBS_SORT | WS_VSCROLL | WS_BORDER)

#define LBN_ERRSPACE (-2)
#define LBN_SELCHANGE 1
#define LBN_DBLCLK 2
#define LBN_SELCANCEL 3
#define LBN_SETFOCUS 4
#define LBN_KILLFOCUS 5

#define LB_OKAY 0
#define LB_ERR (-1)
#define LB_ERRSPACE (-2)

/* LB_DIR / CB_DIR attributes (include/winuser.h 3037-3046) */
#define DDL_READWRITE 0x0000
#define DDL_READONLY 0x0001
#define DDL_HIDDEN 0x0002
#define DDL_SYSTEM 0x0004
#define DDL_DIRECTORY 0x0010
#define DDL_ARCHIVE 0x0020
#define DDL_POSTMSGS 0x2000
#define DDL_DRIVES 0x4000
#define DDL_EXCLUSIVE 0x8000

/* ---- COMBOBOX ---- */
#define CB_GETEDITSEL (WM_USER + 0) /* winuser16.h 234-258 */
#define CB_LIMITTEXT (WM_USER + 1)
#define CB_SETEDITSEL (WM_USER + 2)
#define CB_ADDSTRING (WM_USER + 3)
#define CB_DELETESTRING (WM_USER + 4)
#define CB_DIR (WM_USER + 5)
#define CB_GETCOUNT (WM_USER + 6)
#define CB_GETCURSEL (WM_USER + 7)
#define CB_GETLBTEXT (WM_USER + 8)
#define CB_GETLBTEXTLEN (WM_USER + 9)
#define CB_INSERTSTRING (WM_USER + 10)
#define CB_RESETCONTENT (WM_USER + 11)
#define CB_FINDSTRING (WM_USER + 12)
#define CB_SELECTSTRING (WM_USER + 13)
#define CB_SETCURSEL (WM_USER + 14)
#define CB_SHOWDROPDOWN (WM_USER + 15)
#define CB_GETITEMDATA (WM_USER + 16)
#define CB_SETITEMDATA (WM_USER + 17)
#define CB_GETDROPPEDCONTROLRECT (WM_USER + 18)
#define CB_SETITEMHEIGHT (WM_USER + 19)
#define CB_GETITEMHEIGHT (WM_USER + 20)
#define CB_SETEXTENDEDUI (WM_USER + 21)
#define CB_GETEXTENDEDUI (WM_USER + 22)
#define CB_GETDROPPEDSTATE (WM_USER + 23)
#define CB_FINDSTRINGEXACT (WM_USER + 24)

#define CBS_SIMPLE 0x0001UL
#define CBS_DROPDOWN 0x0002UL
#define CBS_DROPDOWNLIST 0x0003UL
#define CBS_OWNERDRAWFIXED 0x0010UL
#define CBS_OWNERDRAWVARIABLE 0x0020UL
#define CBS_AUTOHSCROLL 0x0040UL
#define CBS_OEMCONVERT 0x0080UL
#define CBS_SORT 0x0100UL
#define CBS_HASSTRINGS 0x0200UL
#define CBS_NOINTEGRALHEIGHT 0x0400UL

#define CBN_ERRSPACE (-1)
#define CBN_SELCHANGE 1
#define CBN_DBLCLK 2
#define CBN_SETFOCUS 3
#define CBN_KILLFOCUS 4
#define CBN_EDITCHANGE 5
#define CBN_EDITUPDATE 6
#define CBN_DROPDOWN 7

#define CB_OKAY 0
#define CB_ERR (-1)
#define CB_ERRSPACE (-2)

/* Bytes of per-window state (cbWndExtra) of the part 2 classes; each .c file has a _Static_assert. */
#define USER_EDIT_EXTRA 40
#define USER_LISTBOX_EXTRA 120
#define USER_COMBO_EXTRA 40

/* Offsets in the callback segment of the control class procedures (0x0000 is DefWindowProc, 0x0010
 * the MessageBox class). 0x0080 is reserved for the dialog manager (stars-0z4.12). */
#define USER_CB_BUTTON MK_FP(MEM_SEL_CALLBACK, 0x0020)
#define USER_CB_STATIC MK_FP(MEM_SEL_CALLBACK, 0x0030)
#define USER_CB_SCROLLBAR MK_FP(MEM_SEL_CALLBACK, 0x0040)
#define USER_CB_EDIT MK_FP(MEM_SEL_CALLBACK, 0x0050)
#define USER_CB_LISTBOX MK_FP(MEM_SEL_CALLBACK, 0x0060)
#define USER_CB_COMBOBOX MK_FP(MEM_SEL_CALLBACK, 0x0070)

/* ---- dialog button helpers (USER.96, .97, .98) ---- */

/* BM_SETCHECK to the child of hDlg with the given id; nothing when there is none. Returns 1 when
 * the control exists. */
uint16_t CheckDlgButton(HWND16 hDlg, int16_t nIDButton, uint16_t uCheck);
/* Checks nIDCheckButton and unchecks every other button whose id is in [nIDFirstButton,
 * nIDLastButton]. Returns 1 when the first and the last button exist, else 0 (UNVERIFIED: the id
 * range is walked numerically). */
uint16_t CheckRadioButton(HWND16 hDlg, int16_t nIDFirstButton, int16_t nIDLastButton, int16_t nIDCheckButton);
/* BM_GETCHECK of the control; 0 when it does not exist. */
uint16_t IsDlgButtonChecked(HWND16 hDlg, int16_t nIDButton);

/* ---- runtime internals (ctl.c, ctl_button.c, ctl_static.c, ctl_scroll.c) ---- */

/* First child of parent whose id is id (GetDlgItem without the dialog manager); 0 when none. The
 * dialog manager (stars-0z4.12) builds GetDlgItem on it. */
HWND16 user_dlg_item(HWND16 parent, int16_t id);

/* Registers the three classes and creates the system brushes; called by user_init. user_ctl_shutdown
 * deletes the brushes (user_shutdown). */
void user_ctl_init(void);
void user_ctl_shutdown(void);

uint32_t user_button_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
uint32_t user_static_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
uint32_t user_scrollbar_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);
uint32_t user_edit_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);     /* ctl_edit.c */
uint32_t user_listbox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp);  /* ctl_listbox.c */
uint32_t user_combobox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp); /* ctl_combo.c */
/* Frees the heap blocks (text, items, clipboard) the part 2 controls keep outside their extra bytes;
 * called by user_ctl_shutdown for every live window, no messages sent. */
void user_edit_release(HWND16 h);
void user_listbox_release(HWND16 h);
void user_edit_clip_release(void);
/* Combo box support: the edit field forwards the keys the combo box handles (F4, Up, Down, Return, Escape). */
void user_edit_set_combo(HWND16 h);
/* Combo box support: sets the text of an edit control (and selects it all) without notifying the parent. */
void user_edit_set_quiet(HWND16 h, const char *s, unsigned n, bool select_all);
/* Text extent of n characters of s (DC font), and the height of a line of the DC font. */
int user_ctl_text_width(HDC16 hdc, const char *s, int n);
int user_ctl_text_height(HDC16 hdc);
/* Sends a WM_COMMAND notification to target (not the parent) on behalf of ctl, with the id the
 * control has. The combo box lists and edit fields report to their combo box. */
void user_ctl_notify_to(HWND16 target, HWND16 ctl, uint16_t id, uint16_t code);
/* The NUL-terminated string at a far pointer (at most max characters, never past the segment limit);
 * NULL when the pointer is invalid. *len gets the length. */
const char *user_ctl_far_str(FARPTR p, unsigned max, unsigned *len);
/* TextOut of a C string at (x, y) in the DC. */
void user_ctl_text_out(HDC16 hdc, int x, int y, const char *s, int n);
/* The edit controls' internal clipboard (Ctrl+Ins, Shift+Del, Shift+Ins, WM_COPY/CUT/PASTE). */
void user_edit_clip_set(const char *s, unsigned n);
unsigned user_edit_clip_get(const char **s);

/* DefWindowProc(WM_CTLCOLOR): sets the colours of hdc for the control type and returns a system
 * brush (win32u/defwnd.c handle_control_color 2335-2366). */
uint32_t user_def_ctlcolor(HDC16 hdc, uint16_t type);
/* Sends WM_CTLCOLOR to the parent of the control (wParam = hdc, LOWORD(lParam) = control, HIWORD =
 * type) and returns the brush it answered, or the default brush when it answered 0. */
uint16_t user_ctl_color(HWND16 ctl, HDC16 hdc, uint16_t type);

/* Pointer to the control's private state in its extra bytes (at least need bytes), or NULL. */
void *user_ctl_extra(HWND16 h, unsigned need);
/* Sends a WM_COMMAND notification to the parent: wParam = id, LOWORD(lParam) = ctl, HIWORD = code. */
void user_ctl_notify(HWND16 ctl, uint16_t code);
/* Fills a rectangle with a brush handle. */
void user_ctl_fill_brush(HDC16 hdc, RECT16 r, uint16_t brush);
/* DrawText of a C string (DT_* flags from runtime/text.h). */
void user_ctl_draw_text(HDC16 hdc, RECT16 r, const char *text, uint16_t flags);
/* Dotted rectangle (alternate pixels). */
void user_ctl_focus_rect(HDC16 hdc, RECT16 r, uint32_t color);
/* The font to select for the control (its WM_SETFONT font, or the system font). */
uint16_t user_ctl_pick_font(uint16_t font);

/* Redraws a scroll bar control now (SetScrollPos / SetScrollRange with bRedraw on SB_CTL). */
void user_ctl_scroll_redraw(HWND16 h);

#endif /* STARS_RUNTIME_CTL_H */
