/* Expectations for the dialog manager (runtime/dialog.c): DialogBox, CreateDialog, EndDialog,
 * GetDlgItem, SetDlgItemText, GetDlgItemText, SendDlgItemMessage, DefDlgProc and the dialog
 * keyboard interface. Task stars-0z4.12, written BEFORE the implementation. Do not edit an
 * expectation to match the code: a disagreement is reported with both values.
 *
 * Sources. The Windows 3.1 SDK text is NOT available on this machine and tests/ref/ holds no
 * reference capture, so NOTHING marked UNVERIFIED below is confirmed against Windows 3.1.
 *   TEMPLATES  the 36 RT_DIALOG resources of original/stars26i.exe, parsed with ne_parse_dialog
 *              (runtime/ne.h) and pinned against the decoded text of
 *              build/nedump --dump-resources re/resources original/stars26i.exe
 *              (re/resources/00NN-dialog-<id>.txt). Fixed facts, verified here: 36 templates,
 *              344 items, every item class is one of the predefined 0x80..0x85 (no custom class),
 *              every template is DS_SETFONT "MS Sans Serif" 8 pt.
 *   WINE       wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0:
 *              dlls/user.exe16/dialog.c (DIALOG_CreateIndirect, DEFDLG_Proc16, DefDlgProc16,
 *              EndDialog16, MapDialogRect16), dlls/user32/dialog.c (IsDialogMessageW:
 *              VK_TAB / arrows / VK_RETURN / VK_ESCAPE / WM_SYSCHAR; GetNextDlgTabItem,
 *              GetNextDlgGroupItem; DEFDLG_SetFocus / DIALOG_FixOneChildOnChangeFocus;
 *              DEFDLG_Proc: the answer of the dialog function is DWL_MSGRESULT except for
 *              WM_INITDIALOG / WM_CTLCOLOR / WM_VKEYTOITEM / WM_CHARTOITEM / WM_COMPAREITEM /
 *              WM_QUERYDRAGICON, which are returned as they are).
 *   ISSUE      the text of bd issue stars-0z4.12 (modal loop, owner disabled, default push
 *              button, Enter / Esc as WM_COMMAND IDOK / IDCANCEL, Tab and arrows, mnemonics,
 *              dialog units from the dialog font, DS_MODALFRAME, WM_SETFONT to every control,
 *              WM_CTLCOLOR with HIWORD(lParam) = CTLCOLOR_*).
 *
 * UNVERIFIED (every item is also in docs/inventory.md section 17):
 *   U1  Dialog base units. Rule used: the dialog font is created from the template's face and
 *       point size (lfHeight = -MulDiv(pt, 96, 72), weight 400), selected into a screen DC, and
 *       the base units are tmAveCharWidth and tmHeight of GetTextMetrics. A template value v in
 *       dialog units becomes MulDiv(v, xbase, 4) (x, cx) or MulDiv(v, ybase, 8) (y, cy), i.e.
 *       ROUNDED (Wine MapDialogRect16 uses MulDiv). Windows 3.1 may truncate instead; the
 *       issue's mutation (truncate) must make test_dialog_units fail.
 *       The pixel values below follow from the Liberation Sans look-alike for MS Sans Serif 8 pt
 *       (runtime/fontdata.c); the real font would give other base units. There is no reference
 *       PNG, so "control rectangles equal the reference capture" is NOT checked, only the
 *       arithmetic (PIXEL_TEST names tests/ref/dialog_about.png, which does not exist).
 *   U2  Dialog position: the template x, y are the CLIENT origin relative to the owner's client
 *       area (screen when there is no owner or DS_ABSALIGN); the window rectangle is the client
 *       rectangle plus the non-client frame; the window is moved to stay on the screen.
 *   U3  Window creation order: the dialog window is created hidden, receives WM_SETFONT, then each
 *       control is created and receives WM_SETFONT, then WM_INITDIALOG (wParam = first WS_TABSTOP
 *       control, lParam = the init parameter), then the dialog is shown. The dialog function does
 *       not see WM_NCCREATE / WM_CREATE (the function is attached after creation).
 *   U4  WM_INITDIALOG answering nonzero: the focus goes to the first WS_TABSTOP control (a push
 *       button there becomes the default button, an edit control has its text selected); answering
 *       zero: the focus is left as the dialog function set it, and when it set none, the first
 *       WS_TABSTOP control gets it when the dialog is activated.
 *   U5  Enter: a focused push button is pressed (WM_COMMAND with its id); otherwise the default
 *       push button (DM_GETDEFID); with none, IDOK. Esc: IDCANCEL. lParam is the control handle
 *       (0 when there is no such control) with BN_CLICKED in the high word. A control answering
 *       DLGC_WANTALLKEYS / DLGC_WANTMESSAGE to WM_GETDLGCODE gets the key itself.
 *   U6  Focus moving onto a push button makes it the default push button (BS_DEFPUSHBUTTON) and the
 *       former one an ordinary push button; focus on anything else restores the DM_SETDEFID /
 *       template default. Mouse clicks do not move the default look.
 *   U7  Arrow keys on a control that does not want them move the focus inside its WS_GROUP group
 *       (wrapping); arriving on a radio button presses it (it checks itself and sends BN_CLICKED).
 *   U8  Alt+letter: an exact '&' mnemonic match on a visible, enabled control. A static control
 *       or group box passes the focus to the next WS_TABSTOP control; a push button, check box or
 *       radio button receives the focus and is pressed. No match: the key is left to the window
 *       procedure.
 *   U9  WM_CLOSE reaching DefDlgProc sends WM_COMMAND IDCANCEL to the dialog (Wine does not).
 *   U10 The dialog background is the WM_CTLCOLOR / CTLCOLOR_DLG brush (COLOR_BTNFACE by default).
 */
#include "test.h"

#include "ctl2_test_common.h"
#include "runtime/ne.h"
#include "runtime/dialog.h"

/* ---- the dialog function used by every test: a registry procedure that logs and defers to a hook ---- */

#define OFF_PROC_D 0x0030
#define FP_PROC_D MK_FP(TSEL, OFF_PROC_D)

typedef struct DL {
  HWND16 h;
  uint16_t msg, wp;
  uint32_t lp;
  uint16_t focus; /* GetFocus() when the message arrived */
} DL;
static DL g_dl[2048];
static int g_ndl;
/* Returns 1 when it answered; *ret is the dialog function's return value. */
static int (*g_dp)(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp, uint32_t *ret);
static HWND16 g_hinst;
static uint8_t *g_exe;
static size_t g_exe_len;

static int logged(uint16_t m) {
  return m == WM_SETFONT || m == WM_INITDIALOG || m == WM_COMMAND || m == WM_CTLCOLOR || m == WM_NEXTDLGCTL ||
         m == WM_USER || m == WM_USER + 5 || m == WM_CLOSE || m == WM_ACTIVATE || m == WM_ERASEBKGND;
}

static uint32_t ad_d(uint16_t ss, uint16_t sp, Regs16 *r) {
  uint32_t lp = DT_ARG32(ss, sp, 4), ret = 0;
  uint16_t wp = DT_ARG16(ss, sp, 8), msg = DT_ARG16(ss, sp, 10), h = DT_ARG16(ss, sp, 12);
  (void)r;
  if (msg == WM_COMMAND && (lp >> 16) != 0) { /* EN_* / LBN_* notifications are not clicks: not logged */
  } else if (logged(msg) && g_ndl < 2048) {
    g_dl[g_ndl].h = h;
    g_dl[g_ndl].msg = msg;
    g_dl[g_ndl].wp = wp;
    g_dl[g_ndl].lp = lp;
    g_dl[g_ndl].focus = GetFocus();
    g_ndl++;
  }
  if (g_dp && g_dp(h, msg, wp, lp, &ret)) return ret;
  return 0;
}

static void reg_d(void) {
  DtEntry d = {TSEL, OFF_PROC_D, "test_dlgproc", FAR_PASCAL, ad_d, 1};
  difftest_register(&d);
}

static int dl_count(uint16_t msg) {
  int n = 0;
  for (int i = 0; i < g_ndl; i++) n += g_dl[i].msg == msg;
  return n;
}
static const DL *dl_nth(uint16_t msg, int nth) {
  for (int i = 0; i < g_ndl; i++)
    if (g_dl[i].msg == msg && nth-- == 0) return &g_dl[i];
  return NULL;
}
/* WM_COMMAND notifications received by the dialog function as "id,id,..." */
static const char *cmds(void) {
  static char out[256];
  size_t n = 0;
  out[0] = 0;
  for (int i = 0, k = 0; i < g_ndl; i++)
    if (g_dl[i].msg == WM_COMMAND) n += (size_t)snprintf(out + n, sizeof out - n, "%s%u", k++ ? "," : "", g_dl[i].wp);
  return out;
}
#define CHECK_CMDS(want)                                                                          \
  do {                                                                                            \
    const char *got_ = cmds();                                                                    \
    test_checks_run++;                                                                            \
    if (strcmp(got_, (want)) != 0) {                                                              \
      test_checks_failed++;                                                                       \
      fprintf(stderr, "%s:%d: WM_COMMAND ids differ\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, got_, (want)); \
    }                                                                                             \
  } while (0)

/* ---- message pump with the dialog manager (what a game with IsDialogMessage would do) ---- */

static HWND16 g_pumpdlg; /* 0: plain pump, no dialog navigation */
static void dpump(void) {
  int guard = 0;
  while (guard++ < 400 && PeekMessage(g_msg, 0, 0, 0, PM_REMOVE)) {
    if (g_pumpdlg && IsDialogMessage(g_pumpdlg, g_msg)) continue;
    TranslateMessage(g_msg);
    DispatchMessage(g_msg);
  }
}
static void kd(unsigned vk, int down) {
  g_t += 1000;
  rt_input_key((uint16_t)vk, 0x10, down, g_t);
  dpump();
}
static void kt(unsigned vk) {
  kd(vk, 1);
  kd(vk, 0);
}
static void kt_shift(unsigned vk) {
  kd(VK_SHIFT, 1);
  kt(vk);
  kd(VK_SHIFT, 0);
}
static void kt_alt(unsigned vk) {
  kd(VK_MENU, 1);
  kt(vk);
  kd(VK_MENU, 0);
}
static HWND16 itm(HWND16 dlg, int id) { return GetDlgItem(dlg, (int16_t)id); }
static int focus_id(HWND16 dlg) {
  HWND16 f = GetFocus();
  if (!f) return -1;
  if (f == dlg) return 0;
  if (GetParent(f) != dlg) return -2;
  return (int)GetWindowWord(f, GWL_ID);
}

/* ---- template builder (host memory) ---- */

typedef struct TB {
  uint8_t b[8192];
  size_t n;
  size_t count_at;
} TB;
static void u8(TB *t, unsigned v) { t->b[t->n++] = (uint8_t)v; }
static void u16(TB *t, unsigned v) {
  u8(t, v & 255);
  u8(t, v >> 8);
}
static void u32(TB *t, uint32_t v) {
  u16(t, v & 0xFFFF);
  u16(t, v >> 16);
}
static void zs(TB *t, const char *s) {
  while (*s) u8(t, (unsigned char)*s++);
  u8(t, 0);
}
static void tb_head(TB *t, uint32_t style, int x, int y, int cx, int cy, const char *caption, int pt, const char *face) {
  memset(t, 0, sizeof *t);
  u32(t, style);
  t->count_at = t->n;
  u8(t, 0);
  u16(t, (unsigned)x);
  u16(t, (unsigned)y);
  u16(t, (unsigned)cx);
  u16(t, (unsigned)cy);
  u8(t, 0); /* no menu */
  u8(t, 0); /* default class */
  zs(t, caption);
  if (style & 0x40) {
    u16(t, (unsigned)pt);
    zs(t, face);
  }
}
/* cls: 0x80 BUTTON, 0x81 EDIT, 0x82 STATIC, 0x83 LISTBOX, 0x84 SCROLLBAR, 0x85 COMBOBOX, or 0 and name */
static void tb_item(TB *t, int x, int y, int cx, int cy, unsigned id, uint32_t style, unsigned cls, const char *name,
                    const char *text) {
  u16(t, (unsigned)x);
  u16(t, (unsigned)y);
  u16(t, (unsigned)cx);
  u16(t, (unsigned)cy);
  u16(t, id);
  u32(t, style);
  if (cls) u8(t, cls);
  else zs(t, name);
  zs(t, text ? text : "");
  u8(t, 0);
  t->b[t->count_at]++;
}

/* Dialog base units of "MS Sans Serif" 8 pt (lfHeight -11) in the Liberation Sans look-alike set:
 * tmAveCharWidth 5, tmHeight 13 (runtime/fontdata.c). The real MS Sans Serif 8 pt is believed to
 * give 5 x 13 as well (UNVERIFIED, U1). */
#define BASE_X 5
#define BASE_Y 13

#define DLG_STYLE_VIS (WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT) /* 0x90c800c0 */
#define CH (WS_CHILD | WS_VISIBLE)
#define BTN 0x80
#define EDT 0x81
#define STA 0x82
#define LST 0x83
#define SCR 0x84
#define CMB 0x85

/* ---- the 36 templates of the game (decoded text of nedump --dump-resources) ---- */

typedef struct TItem {
  unsigned id;
  uint32_t style;
  const char *cls;
  const char *text; /* prefix (up to 14 characters) of the text; NULL when empty */
} TItem;
typedef struct TDlg {
  unsigned id;
  uint32_t style;
  int x, y, cx, cy, items;
  TItem first, last;
} TDlg;
static const TDlg TEMPLATES[36] = {
  {90, 0x00c800c0UL, 17, 15, 192, 150, 8, {65535, 0x50020003UL, "STATIC", "StarsIco"}, {1, 0x50030001UL, "BUTTON", "OK"}},
  {97, 0x00c800c0UL, 17, 15, 172, 84, 5, {65535, 0x50020001UL, "STATIC", "To order addit"}, {1, 0x50030001UL, "BUTTON", "OK"}},
  {1068, 0x90c800c0UL, 0, 0, 186, 115, 5, {65535, 0x50020000UL, "STATIC", "Do you want to"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {2025, 0x90c800c0UL, 0, 0, 186, 95, 4, {65535, 0x50020000UL, "STATIC", "Do you want to"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {91, 0x80c800c0UL, 11, 52, 280, 163, 3, {1, 0x50010001UL, "BUTTON", "OK"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {214, 0x90c800c0UL, 0, 0, 180, 70, 7, {268, 0x50830080UL, "EDIT", NULL}, {118, 0x50030000UL, "BUTTON", "&Help"}},
  {209, 0x90c800c0UL, 0, 0, 240, 190, 16, {200, 0x50020009UL, "BUTTON", "Easy"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {390, 0x90c800c0UL, 0, 0, 261, 210, 27, {65535, 0x50020002UL, "STATIC", "&Game Name:"}, {1072, 0x50010001UL, "BUTTON", "&Finish"}},
  {391, 0x90c800c0UL, 0, 0, 261, 210, 5, {1072, 0x50010001UL, "BUTTON", "&Finish"}, {1071, 0x50010000UL, "BUTTON", "&Next >"}},
  {392, 0x90c800c0UL, 0, 0, 261, 210, 13, {65535, 0x50020000UL, "STATIC", "Victory is dec"}, {1072, 0x50010001UL, "BUTTON", "&Finish"}},
  {160, 0x90c800c0UL, 0, 0, 260, 270, 7, {161, 0x50010000UL, "BUTTON", "|<<"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {115, 0x90c800c0UL, 0, 0, 260, 220, 15, {65535, 0x50020002UL, "STATIC", "Game:"}, {2017, 0x50020001UL, "STATIC", NULL}},
  {1026, 0x90c800c0UL, 0, 0, 155, 115, 9, {65535, 0x50020000UL, "STATIC", "Auto Generate:"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {92, 0x80c800c0UL, 11, 52, 351, 250, 15, {1, 0x50010001UL, "BUTTON", "OK"}, {2075, 0x40810080UL, "EDIT", NULL}},
  {146, 0x90c800c0UL, 0, 0, 261, 209, 21, {65535, 0x50020002UL, "STATIC", "Race Name:"}, {1072, 0x50010000UL, "BUTTON", "&Finish"}},
  {147, 0x90c800c0UL, 0, 0, 261, 209, 8, {291, 0x50010003UL, "BUTTON", "Immune to &Gra"}, {1072, 0x50010000UL, "BUTTON", "&Finish"}},
  {148, 0x90c800c0UL, 0, 0, 261, 209, 6, {291, 0x50010003UL, "BUTTON", "Factories cost"}, {1072, 0x50010000UL, "BUTTON", "&Finish"}},
  {149, 0x90c800c0UL, 0, 0, 261, 209, 15, {271, 0x50020009UL, "BUTTON", "Hyper-Expansio"}, {1072, 0x50010001UL, "BUTTON", "&Finish"}},
  {150, 0x90c800c0UL, 0, 0, 261, 209, 20, {65535, 0x50020001UL, "STATIC", "Lesser Racial "}, {1072, 0x50010000UL, "BUTTON", "&Finish"}},
  {151, 0x90c800c0UL, 0, 0, 261, 209, 24, {271, 0x50020009UL, "BUTTON", "Costs 75% extr"}, {1072, 0x50010001UL, "BUTTON", "&Finish"}},
  {93, 0x90c800c0UL, 0, 0, 294, 191, 13, {1046, 0x50a10051UL, "LISTBOX", NULL}, {2, 0x50010000UL, "BUTTON", "Cancel"}},
  {82, 0x90c800c0UL, 0, 0, 200, 150, 7, {65535, 0x50020000UL, "STATIC", "Select the fle"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {2013, 0x90c800c0UL, 0, 0, 239, 115, 16, {2071, 0x50010000UL, "BUTTON", "&Delete"}, {1053, 0x50010003UL, "BUTTON", "D&ump Cargo"}},
  {2008, 0x90c800c0UL, 0, 0, 198, 80, 7, {2, 0x50010001UL, "BUTTON", "Close"}, {2006, 0x50000009UL, "BUTTON", "&Enemy"}},
  {127, 0x90c800c0UL, 0, 0, 350, 262, 9, {1073, 0x50000009UL, "BUTTON", NULL}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {89, 0x90c800c0UL, 0, 0, 170, 132, 11, {1073, 0x50000009UL, "BUTTON", NULL}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {393, 0x90c000c0UL, 0, 0, 160, 40, 1, {1071, 0x50020001UL, "STATIC", "Generating dat"}, {1071, 0x50020001UL, "STATIC", "Generating dat"}},
  {128, 0x90c800c0UL, 0, 0, 349, 247, 5, {1070, 0x50010000UL, "BUTTON", "<- Prev"}, {2, 0x50010001UL, "BUTTON", "Close"}},
  {2502, 0x90c800c0UL, 0, 0, 145, 184, 3, {2, 0x50010001UL, "BUTTON", "Hide"}, {2503, 0x50010000UL, "BUTTON", "Panic!"}},
  {2504, 0x90c800c0UL, 0, 0, 174, 206, 8, {2505, 0x50010000UL, "BUTTON", "&Redo Turn"}, {65535, 0x50020000UL, "STATIC", "This button di"}},
  {140, 0x90c800c0UL, 0, 0, 145, 60, 5, {2018, 0x50020000UL, "STATIC", NULL}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {141, 0x90c800c0UL, 0, 0, 199, 70, 8, {65535, 0x50020002UL, "STATIC", "New Password:"}, {2018, 0x50020000UL, "STATIC", NULL}},
  {2019, 0x90c800c0UL, 0, 0, 162, 55, 5, {65535, 0x50020000UL, "STATIC", "New Name:"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {86, 0x90c800c0UL, 0, 0, 170, 98, 5, {65535, 0x50020002UL, "STATIC", "Serial #:"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {4202, 0x90c800c0UL, 0, 0, 162, 55, 5, {65535, 0x50020000UL, "STATIC", "&Find:"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
  {102, 0x90c800c0UL, 0, 0, 360, 220, 3, {198, 0x50010000UL, "BUTTON", "&Switch"}, {118, 0x50010000UL, "BUTTON", "&Help"}},
};

static const char *cls_name(const char *c) {
  static const char *n[] = {"BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX"};
  if (c[0] == '#' && atoi(c + 1) >= 0x80 && atoi(c + 1) <= 0x85) return n[atoi(c + 1) - 0x80];
  return c;
}

static int load_exe(void) {
  FILE *fp = fopen(EXE, "rb");
  if (!fp) return 1;
  fseek(fp, 0, SEEK_END);
  g_exe_len = (size_t)ftell(fp);
  fseek(fp, 0, SEEK_SET);
  g_exe = malloc(g_exe_len);
  if (!g_exe || fread(g_exe, 1, g_exe_len, fp) != g_exe_len) return 1;
  fclose(fp);
  return 0;
}

static void test_constants(void) {
  CHECK_EQ(WM_INITDIALOG, 0x0110);
  CHECK_EQ(WM_NEXTDLGCTL, 0x0028);
  CHECK_EQ(DM_GETDEFID, 0x0400);
  CHECK_EQ(DM_SETDEFID, 0x0401);
  CHECK_EQ(DC_HASDEFID, 0x534B);
  CHECK_EQ(DWL_MSGRESULT, 0);
  CHECK_EQ(DWL_DLGPROC, 4);
  CHECK_EQ(DWL_USER, 8);
  CHECK_EQ(DLGWINDOWEXTRA, 30);
  CHECK_EQ(DS_MODALFRAME, 0x80);
  CHECK_EQ(DS_SETFONT, 0x40);
  CHECK_EQ(DS_ABSALIGN, 1);
  CHECK_EQ(FP_SEG(USER_CB_DIALOG), MEM_SEL_CALLBACK);
  CHECK_EQ(FP_OFF(USER_CB_DIALOG), 0x0080);
  CHECK_EQ(IDOK, 1);
  CHECK_EQ(IDCANCEL, 2);
}

/* ---- all 36 templates parse, with pinned counts and first / last items ---- */

static void test_templates_parse(void) {
  NeFile nf;
  unsigned n_dlg = 0, n_items = 0, n_custom = 0, n_font = 0;
  CHECK_EQ(ne_parse(g_exe, g_exe_len, &nf), NE_OK);
  for (size_t i = 0; i < nf.n_resources; i++) {
    const NeResource *r = &nf.resources[i];
    const TDlg *t = NULL;
    NeDialog d;
    if (r->type_name || r->type != RT_DIALOG) continue;
    n_dlg++;
    for (int k = 0; k < 36; k++)
      if (TEMPLATES[k].id == r->id) t = &TEMPLATES[k];
    CHECK(t != NULL);
    CHECK_EQ(ne_parse_dialog(g_exe + r->offset, r->size, &d), NE_OK);
    if (!t || !d.items) {
      ne_dialog_free(&d);
      continue;
    }
    CHECK_EQ(d.style, t->style);
    CHECK_EQ(d.x, t->x);
    CHECK_EQ(d.y, t->y);
    CHECK_EQ(d.cx, t->cx);
    CHECK_EQ(d.cy, t->cy);
    CHECK_EQ(d.item_count, t->items);
    CHECK_EQ(d.n_items, (size_t)t->items);
    CHECK(d.has_font);
    CHECK_EQ(d.font_size, 8);
    CHECK(d.font_name && strcmp(d.font_name, "MS Sans Serif") == 0);
    CHECK(d.menu == NULL);
    CHECK(d.cls == NULL);
    n_font += d.has_font;
    n_items += d.n_items;
    for (int e = 0; e < 2; e++) {
      const TItem *want = e ? &t->last : &t->first;
      const NeDialogItem *it = &d.items[e ? d.n_items - 1 : 0];
      CHECK_EQ(it->id, want->id);
      CHECK_EQ(it->style, want->style);
      CHECK(strcmp(cls_name(it->cls), want->cls) == 0);
      if (!want->text) CHECK(it->text == NULL);
      else CHECK(it->text && strncmp(it->text, want->text, strlen(want->text)) == 0);
    }
    for (size_t k = 0; k < d.n_items; k++) {
      const char *c = d.items[k].cls;
      if (!(c[0] == '#' && atoi(c + 1) >= 0x80 && atoi(c + 1) <= 0x85)) n_custom++;
    }
    ne_dialog_free(&d);
  }
  CHECK_EQ(n_dlg, 36);
  CHECK_EQ(n_items, 344);
  CHECK_EQ(n_custom, 0); /* no custom control class in any template (docs/inventory.md section 17) */
  CHECK_EQ(n_font, 36);
  ne_free(&nf);
}

/* ---- control creation for every template ---- */

static void test_create_all_templates(void) {
  begin();
  for (int k = 0; k < 36; k++) {
    const TDlg *t = &TEMPLATES[k];
    NeFile nf;
    NeDialog d;
    const NeResource *r;
    HWND16 dlg;
    unsigned nchild = 0;
    CHECK_EQ(ne_parse(g_exe, g_exe_len, &nf), NE_OK);
    r = ne_find_resource(&nf, RT_DIALOG, (uint16_t)t->id);
    CHECK(r != NULL);
    if (!r) {
      ne_free(&nf);
      continue;
    }
    CHECK_EQ(ne_parse_dialog(g_exe + r->offset, r->size, &d), NE_OK);
    g_ndl = 0;
    g_dp = NULL;
    dlg = CreateDialog(g_hinst, MK_FP(0, t->id), g_top, FP_PROC_D);
    CHECK(dlg != 0);
    if (dlg) {
      const UserState *st = user_state();
      const UWnd *w = user_wnd(dlg);
      /* WS_VISIBLE of the template decides whether CreateDialog shows it */
      CHECK_EQ((w->style & WS_VISIBLE) != 0, (t->style & WS_VISIBLE) != 0);
      CHECK_EQ(GetWindowLong(dlg, GWL_WNDPROC), USER_CB_DIALOG);
      CHECK_EQ(GetWindowLong(dlg, DWL_DLGPROC), FP_PROC_D);
      CHECK_EQ((GetWindowLong(dlg, GWL_EXSTYLE) & WS_EX_DLGMODALFRAME) != 0, (t->style & DS_MODALFRAME) != 0);
      for (const UWnd *c = user_wnd(w->child); c; c = user_wnd(c->next)) {
        const NeDialogItem *it;
        if (nchild >= d.n_items) {
          nchild++;
          continue;
        }
        it = &d.items[nchild];
        CHECK_EQ(c->id, it->id);
        CHECK(strcmp(st->c[c->cls].name, cls_name(it->cls)) == 0);
        /* the window style keeps the template's high word (the list box / combo box classes drop WS_VSCROLL
         * themselves: they draw their own scroll bars) */
        CHECK_EQ(c->style & 0xFFCF0000u, (it->style | WS_CHILD) & 0xFFCF0000u);
        if (it->text) CHECK(strncmp(c->text, it->text, UTEXT_MAX - 1) == 0);
        else CHECK_EQ(c->text[0], 0);
        /* WM_SETFONT reached every control (the template has DS_SETFONT) */
        CHECK(SendMessage(user_hwnd(c), WM_GETFONT, 0, 0) != 0);
        nchild++;
      }
      CHECK_EQ(nchild, d.n_items);
      CHECK_EQ(nchild, (unsigned)t->items);
      CHECK_EQ(dl_count(WM_SETFONT), 1);
      CHECK_EQ(dl_count(WM_INITDIALOG), 1);
      DestroyWindow(dlg);
      dpump();
    }
    CHECK_EQ(user_window_count(), 1); /* only the test's parent window is left */
    ne_dialog_free(&d);
    ne_free(&nf);
  }
  end();
}

/* ---- dialog units ---- */

static void test_dialog_units(void) {
  /* About box 90: 192 x 150 dialog units at (17, 15), DS_MODALFRAME, MS Sans Serif 8 pt. The base
   * units are the average character width and the height of the dialog font (U1). */
  HWND16 dlg;
  int xb = 0, yb = 0;
  RECT16 r;
  begin();
  g_dp = NULL;
  g_ndl = 0;
  dlg = CreateDialog(g_hinst, MK_FP(0, 90), g_top, FP_PROC_D);
  CHECK(dlg != 0);
  CHECK(user_dialog_units(dlg, &xb, &yb));
  CHECK_EQ(xb, BASE_X);
  CHECK_EQ(yb, BASE_Y);
  /* client rectangle = the template size in pixels */
  r = get_rect(dlg, 1);
  CHECK_EQ(r.right - r.left, MulDiv(192, BASE_X, 4));
  CHECK_EQ(r.bottom - r.top, MulDiv(150, BASE_Y, 8));
  /* literal pixel rectangles (client coordinates of the dialog) of the 8 items of template 90;
   * computed by hand from base units BASE_X x BASE_Y with rounding. UNVERIFIED (U1): no tests/ref PNG. */
  PIXEL_TEST("dialog_about");
  {
    static const struct {
      int id, l, t, r, b;
    } want[8] = {
      /* template 90, rounded: x = MulDiv(v, 5, 4), y = MulDiv(v, 13, 8). Worked by hand: item 0 is 8,8 18x20
       * DLU = x 10 (10.0), y 13 (13.0), cx 23 (22.5), cy 33 (32.5). Truncation would give 22 and 32. */
      {65535, 10, 13, 33, 46},   /* StarsIco   8,8   18x20 */
      {65535, 45, 13, 240, 26},  /* 36,8   156x8 */
      {65535, 45, 29, 240, 57},  /* 36,18  156x17 */
      {1025, 5, 52, 235, 65},    /* 4,32   184x8 */
      {65535, 5, 78, 235, 91},   /* 4,48   184x8 */
      {1055, 5, 98, 235, 205},   /* 4,60   184x66 */
      {118, 40, 211, 100, 234},  /* 32,130 48x14 */
      {1, 140, 211, 200, 234},   /* 112,130 48x14 */
    };
    int k = 0;
    const UWnd *w = user_wnd(dlg);
    for (const UWnd *c = user_wnd(w->child); c && k < 8; c = user_wnd(c->next), k++) {
      RECT16 q = c->wrect;
      CHECK_EQ(c->id, (unsigned)want[k].id & 0xFFFF);
      CHECK_EQ(q.left, want[k].l);
      CHECK_EQ(q.top, want[k].t);
      CHECK_EQ(q.right, want[k].r);
      CHECK_EQ(q.bottom, want[k].b);
    }
    CHECK_EQ(k, 8);
  }
  /* position (U2): the client origin is the owner's client origin plus the converted x, y */
  {
    RECT16 cs;
    CHECK(user_client_rect_screen(dlg, &cs));
    CHECK_EQ(cs.left, PX0 + MulDiv(17, BASE_X, 4));
    CHECK_EQ(cs.top, PY0 + MulDiv(15, BASE_Y, 8));
  }
  DestroyWindow(dlg);
  dpump();
  end();
}

/* rounding matters: a synthetic template whose values land on .5 and .75 of a pixel */
static void test_dialog_units_rounding(void) {
  TB t;
  HWND16 dlg;
  int xb, yb;
  static const int v[] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 13};
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 100, 80, "U", 8, "MS Sans Serif");
  for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) tb_item(&t, v[i], v[i], v[i], v[i], 100 + i, CH | WS_BORDER, STA, "", "x");
  g_dp = NULL;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK(user_dialog_units(dlg, &xb, &yb));
  for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
    RECT16 q = user_wnd(itm(dlg, 100 + (int)i))->wrect;
    CHECK_EQ(q.left, MulDiv((int16_t)v[i], (int16_t)xb, 4));
    CHECK_EQ(q.top, MulDiv((int16_t)v[i], (int16_t)yb, 8));
    CHECK_EQ(q.right - q.left, MulDiv((int16_t)v[i], (int16_t)xb, 4));
    CHECK_EQ(q.bottom - q.top, MulDiv((int16_t)v[i], (int16_t)yb, 8));
  }
  /* at least one of these values differs between rounding and truncation, otherwise this test
   * would not tell the mutation apart */
  {
    int differs = 0;
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
      differs |= (MulDiv((int16_t)v[i], (int16_t)xb, 4) != v[i] * xb / 4) || (MulDiv((int16_t)v[i], (int16_t)yb, 8) != v[i] * yb / 8);
    CHECK(differs);
  }
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- API helpers ---- */

static void test_item_access(void) {
  HWND16 dlg, lb, ed;
  FARPTR buf = far_alloc(64);
  begin();
  g_dp = NULL;
  dlg = CreateDialog(g_hinst, MK_FP(0, 93), g_top, FP_PROC_D); /* has LISTBOX 1046 and BUTTON 2 */
  CHECK(dlg != 0);
  lb = GetDlgItem(dlg, 1046);
  CHECK(lb != 0);
  CHECK_EQ(GetParent(lb), dlg);
  CHECK_EQ(GetDlgItem(dlg, 31999), 0);
  CHECK_EQ(GetDlgItem(0xFFFF, 1046), 0);
  CHECK_EQ(GetDlgItem(dlg, 2), user_dlg_item(dlg, 2));
  CHECK_EQ(SendDlgItemMessage(dlg, 1046, LB_ADDSTRING, 0, fstr("alpha")), 0);
  CHECK_EQ(SendDlgItemMessage(dlg, 1046, LB_ADDSTRING, 0, fstr("beta")), 1);
  CHECK_EQ(SendDlgItemMessage(dlg, 1046, LB_GETCOUNT, 0, 0), 2);
  CHECK_EQ(SendDlgItemMessage(dlg, 31999, LB_GETCOUNT, 0, 0), 0); /* no such control */
  SetDlgItemText(dlg, 2, fstr("Quit"));
  CHECK_EQ(GetDlgItemText(dlg, 2, buf, 64), 4);
  CHECK_EQ(strcmp(LIN(buf), "Quit"), 0);
  CHECK_EQ(GetDlgItemText(dlg, 2, buf, 3), 2); /* cut to nMaxCount - 1 */
  CHECK_EQ(strcmp(LIN(buf), "Qu"), 0);
  memcpy(LIN(buf), "zzz", 4);
  CHECK_EQ(GetDlgItemText(dlg, 31999, buf, 64), 0);
  CHECK_EQ(((char *)LIN(buf))[0], 0);
  ed = 0;
  (void)ed;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- creation sequence, WM_SETFONT, WM_INITDIALOG ---- */

static int dp_ret_true(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)h;
  (void)wp;
  (void)lp;
  if (m == WM_INITDIALOG) {
    *ret = 1;
    return 1;
  }
  return 0;
}

static void edit_dialog(TB *t, uint32_t style) {
  tb_head(t, style, 10, 10, 150, 80, "T", 8, "MS Sans Serif");
  tb_item(t, 6, 6, 60, 12, 10, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "hello");
  tb_item(t, 6, 24, 36, 14, 1, CH | WS_TABSTOP | 1, BTN, "", "OK");
  tb_item(t, 50, 24, 36, 14, 2, CH | WS_TABSTOP, BTN, "", "Cancel");
  tb_item(t, 94, 24, 36, 14, 3, CH, BTN, "", "&Apply");
}

static HWND16 want_focus;
static int dp_focus_btn(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)wp;
  (void)lp;
  if (m == WM_INITDIALOG) {
    want_focus = GetDlgItem(h, 2);
    SetFocus(want_focus);
    *ret = 0;
    return 1;
  }
  return 0;
}

static void test_init_sequence(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0x00050007u);
  CHECK(dlg != 0);
  /* U3: WM_SETFONT first, then WM_INITDIALOG with the first tab stop and the init parameter */
  CHECK(g_ndl >= 2);
  CHECK_EQ(g_dl[0].msg, WM_SETFONT);
  CHECK(g_dl[0].wp != 0); /* the dialog font */
  CHECK_EQ(g_dl[1].msg, WM_INITDIALOG);
  CHECK_EQ(g_dl[1].wp, itm(dlg, 10));
  CHECK_EQ(g_dl[1].lp, 0x00050007u);
  /* every control got the dialog font */
  {
    uint32_t f = SendMessage(itm(dlg, 10), WM_GETFONT, 0, 0);
    CHECK(f != 0);
    CHECK_EQ(f, g_dl[0].wp);
    CHECK_EQ(SendMessage(itm(dlg, 1), WM_GETFONT, 0, 0), f);
    CHECK_EQ(SendMessage(itm(dlg, 2), WM_GETFONT, 0, 0), f);
    CHECK_EQ(SendMessage(itm(dlg, 3), WM_GETFONT, 0, 0), f);
  }
  /* U4: answering TRUE gives the focus to the first WS_TABSTOP control, shown, active dialog */
  CHECK(IsWindowVisible(dlg));
  CHECK_EQ(GetActiveWindow(), dlg);
  CHECK_EQ(GetFocus(), itm(dlg, 10));
  CHECK_EQ(GetWindowLong(dlg, DWL_DLGPROC), FP_PROC_D);
  /* the edit control's text is selected when it gets the focus through the dialog manager */
  CHECK_EQ(SendDlgItemMessage(dlg, 10, EM_GETSEL, 0, 0), MKL(0, 5));
  /* DM_GETDEFID: the template's BS_DEFPUSHBUTTON */
  CHECK_EQ(SendMessage(dlg, DM_GETDEFID, 0, 0), MKL(1, DC_HASDEFID));
  DestroyWindow(dlg);
  dpump();
  end();

  /* answering FALSE: the dialog function chose its own focus (the second button) */
  begin();
  {
    edit_dialog(&t, DLG_STYLE_VIS);
    g_dp = dp_focus_btn;
    g_ndl = 0;
    dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
    CHECK(dlg != 0);
    CHECK(want_focus != 0);
    /* U4: answering FALSE leaves the focus where the dialog function put it (SetFocus works inside
     * WM_INITDIALOG even though the dialog is still hidden) */
    CHECK_EQ(GetActiveWindow(), dlg);
    CHECK_EQ(GetFocus(), want_focus);
    CHECK_EQ(GetFocus(), itm(dlg, 2));
    DestroyWindow(dlg);
    dpump();
  }
  end();

  /* a template without WS_VISIBLE stays hidden for CreateDialog */
  begin();
  edit_dialog(&t, DLG_STYLE_VIS & ~WS_VISIBLE);
  g_dp = NULL;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK(!IsWindowVisible(dlg));
  CHECK_EQ(dl_count(WM_INITDIALOG), 1);
  DestroyWindow(dlg);
  dpump();
  end();
}

static void test_dialog_without_font_and_frame(void) {
  TB t;
  HWND16 dlg;
  int xb, yb, sxb, syb;
  begin();
  /* no DS_SETFONT, no DS_MODALFRAME: system font base units, ordinary frame, no WM_SETFONT */
  tb_head(&t, WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU, 0, 0, 100, 50, "N", 0, "");
  tb_item(&t, 4, 4, 40, 12, 5, CH | WS_TABSTOP, BTN, "", "B");
  g_dp = NULL;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK_EQ(dl_count(WM_SETFONT), 0);
  CHECK_EQ(GetWindowLong(dlg, GWL_EXSTYLE) & WS_EX_DLGMODALFRAME, 0);
  CHECK(user_dialog_units(dlg, &xb, &yb));
  {
    HDC16 dc = GetDC(0);
    FARPTR tm = far_alloc(32);
    uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    GetTextMetrics(dc, tm);
    sxb = ((uint8_t *)LIN(tm))[10] | (((uint8_t *)LIN(tm))[11] << 8);
    syb = ((uint8_t *)LIN(tm))[0] | (((uint8_t *)LIN(tm))[1] << 8);
    SelectObject(dc, old);
    ReleaseDC(0, dc);
  }
  CHECK_EQ(xb, sxb);
  CHECK_EQ(yb, syb);
  CHECK_EQ(SendMessage(itm(dlg, 5), WM_GETFONT, 0, 0), 0); /* system font: no font was set */
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- Tab order, default button, Enter and Esc (modeless, with IsDialogMessage) ---- */

static void test_tab_order(void) {
  TB t;
  HWND16 dlg;
  begin();
  /* 10 edit (tab), 20 static, 11 edit disabled (tab), 12 button hidden (tab), 13 button no tab,
   * 14 check (tab), 15 button (tab), 16 edit (tab) */
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 200, 100, "Tabs", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 12, 10, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "e10");
  tb_item(&t, 4, 20, 40, 10, 20, CH, STA, "", "label");
  tb_item(&t, 4, 34, 40, 12, 11, CH | WS_BORDER | WS_TABSTOP | WS_DISABLED | ES_AUTOHSCROLL, EDT, "", "e11");
  tb_item(&t, 4, 48, 40, 12, 12, WS_CHILD | WS_TABSTOP, BTN, "", "hidden");
  tb_item(&t, 4, 62, 40, 12, 13, CH, BTN, "", "notab");
  tb_item(&t, 60, 4, 50, 12, 14, CH | WS_TABSTOP | 3, BTN, "", "check");
  tb_item(&t, 60, 20, 40, 14, 15, CH | WS_TABSTOP, BTN, "", "push");
  tb_item(&t, 60, 40, 40, 12, 16, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "e16");
  g_dp = dp_ret_true;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  CHECK_EQ(focus_id(dlg), 10);
  /* Tab follows template order and skips controls without WS_TABSTOP, disabled and hidden ones */
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 14);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 15);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 16);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 10); /* wraps */
  kt_shift(VK_TAB);
  CHECK_EQ(focus_id(dlg), 16); /* Shift+Tab goes back, wrapping */
  kt_shift(VK_TAB);
  CHECK_EQ(focus_id(dlg), 15);
  /* the same through the helper */
  CHECK_EQ(user_dlg_next_tab(dlg, itm(dlg, 10), false), itm(dlg, 14));
  CHECK_EQ(user_dlg_next_tab(dlg, itm(dlg, 16), false), itm(dlg, 10));
  CHECK_EQ(user_dlg_next_tab(dlg, itm(dlg, 10), true), itm(dlg, 16));
  CHECK_EQ(user_dlg_next_tab(dlg, 0, false), itm(dlg, 10));
  /* WM_NEXTDLGCTL: wParam = control with LOWORD(lParam) != 0, else wParam = 0 next / 1 previous */
  SendMessage(dlg, WM_NEXTDLGCTL, itm(dlg, 16), 1);
  CHECK_EQ(focus_id(dlg), 16);
  SendMessage(dlg, WM_NEXTDLGCTL, 0, 0);
  CHECK_EQ(focus_id(dlg), 10);
  SendMessage(dlg, WM_NEXTDLGCTL, 1, 0);
  CHECK_EQ(focus_id(dlg), 16);
  /* Tab onto an edit control selects its whole text */
  SendMessage(dlg, WM_NEXTDLGCTL, itm(dlg, 15), 1);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 16);
  CHECK_EQ(SendDlgItemMessage(dlg, 16, EM_GETSEL, 0, 0), MKL(0, 3));
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* The game does not import IsDialogMessage: a modeless dialog without it gets no navigation. */
static void test_modeless_without_isdialogmessage(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = 0;
  CHECK_EQ(focus_id(dlg), 10);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 10); /* the Tab key did nothing */
  kt(VK_RETURN);
  CHECK_EQ(dl_count(WM_COMMAND), 0); /* no IDOK either */
  kt(VK_ESCAPE);
  CHECK_EQ(dl_count(WM_COMMAND), 0);
  DestroyWindow(dlg);
  dpump();
  end();
}

static void test_enter_esc_default_button(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  /* focus on the edit control: Enter presses the default push button (id 1) */
  CHECK_EQ(focus_id(dlg), 10);
  kt(VK_RETURN);
  CHECK_CMDS("1");
  {
    const DL *c = dl_nth(WM_COMMAND, 0);
    CHECK(c != NULL);
    CHECK_EQ(c->lp, MKL(itm(dlg, 1), BN_CLICKED));
  }
  /* Esc: IDCANCEL with the cancel button */
  kt(VK_ESCAPE);
  CHECK_CMDS("1,2");
  CHECK_EQ(dl_nth(WM_COMMAND, 1)->lp, MKL(itm(dlg, 2), BN_CLICKED));
  /* U6: tab to Cancel: it becomes the default push button, OK an ordinary one */
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 1);
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 2);
  CHECK_EQ(GetWindowLong(itm(dlg, 2), GWL_STYLE) & BS_TYPEMASK, BS_DEFPUSHBUTTON);
  CHECK_EQ(GetWindowLong(itm(dlg, 1), GWL_STYLE) & BS_TYPEMASK, BS_PUSHBUTTON);
  CHECK_EQ(SendMessage(dlg, DM_GETDEFID, 0, 0), MKL(1, DC_HASDEFID)); /* the real default is still OK */
  g_ndl = 0;
  kt(VK_RETURN); /* a focused push button is pressed */
  CHECK_CMDS("2");
  /* back to the edit control: OK is the default look again */
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 10);
  CHECK_EQ(GetWindowLong(itm(dlg, 1), GWL_STYLE) & BS_TYPEMASK, BS_DEFPUSHBUTTON);
  CHECK_EQ(GetWindowLong(itm(dlg, 2), GWL_STYLE) & BS_TYPEMASK, BS_PUSHBUTTON);
  /* DM_SETDEFID moves the default */
  CHECK_EQ(SendMessage(dlg, DM_SETDEFID, 2, 0), 1);
  CHECK_EQ(SendMessage(dlg, DM_GETDEFID, 0, 0), MKL(2, DC_HASDEFID));
  CHECK_EQ(GetWindowLong(itm(dlg, 2), GWL_STYLE) & BS_TYPEMASK, BS_DEFPUSHBUTTON);
  CHECK_EQ(GetWindowLong(itm(dlg, 1), GWL_STYLE) & BS_TYPEMASK, BS_PUSHBUTTON);
  g_ndl = 0;
  kt(VK_RETURN);
  CHECK_CMDS("2");
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();

  /* no default push button: Enter sends IDOK (with the IDOK control when there is one, else 0) */
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 120, 60, "NoDef", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 60, 12, 10, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "x");
  tb_item(&t, 4, 24, 36, 14, 77, CH | WS_TABSTOP, BTN, "", "Go");
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  CHECK_EQ(SendMessage(dlg, DM_GETDEFID, 0, 0), 0);
  kt(VK_RETURN);
  CHECK_CMDS("1");
  CHECK_EQ(dl_nth(WM_COMMAND, 0)->lp, MKL(0, BN_CLICKED));
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- arrow keys in a WS_GROUP group ---- */

static int radio_checks(HWND16 dlg, int first) {
  int bits = 0;
  for (int i = 0; i < 5; i++) bits |= (int)(IsDlgButtonChecked(dlg, (int16_t)(first + i)) != 0) << i;
  return bits;
}

static void test_arrows_group(void) {
  TB t;
  HWND16 dlg;
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 200, 120, "Groups", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 60, 10, 101, CH | WS_GROUP | WS_TABSTOP | 9, BTN, "", "r1");
  tb_item(&t, 4, 16, 60, 10, 102, CH | 9, BTN, "", "r2");
  tb_item(&t, 4, 28, 60, 10, 103, CH | 9, BTN, "", "r3");
  tb_item(&t, 80, 4, 60, 10, 104, CH | WS_GROUP | WS_TABSTOP | 9, BTN, "", "r4");
  tb_item(&t, 80, 16, 60, 10, 105, CH | 9, BTN, "", "r5");
  tb_item(&t, 80, 40, 60, 12, 110, CH | WS_BORDER | WS_GROUP | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "text");
  tb_item(&t, 4, 60, 40, 14, 1, CH | WS_GROUP | WS_TABSTOP | 1, BTN, "", "OK");
  g_dp = dp_ret_true;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  CHECK_EQ(focus_id(dlg), 101);
  /* U7: Down / Right go to the next control of the group, wrapping at the end; Up / Left back */
  kt(VK_DOWN);
  CHECK_EQ(focus_id(dlg), 102);
  CHECK_EQ(radio_checks(dlg, 101), 0x2); /* arriving on an auto radio button presses it */
  kt(VK_RIGHT);
  CHECK_EQ(focus_id(dlg), 103);
  CHECK_EQ(radio_checks(dlg, 101), 0x4);
  kt(VK_DOWN);
  CHECK_EQ(focus_id(dlg), 101); /* wraps inside the group: it does not leave for r4 */
  CHECK_EQ(radio_checks(dlg, 101), 0x1);
  kt(VK_UP);
  CHECK_EQ(focus_id(dlg), 103);
  kt(VK_LEFT);
  CHECK_EQ(focus_id(dlg), 102);
  /* Tab leaves the group for the next WS_TABSTOP control, r4 */
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 104);
  kt(VK_DOWN);
  CHECK_EQ(focus_id(dlg), 105);
  CHECK_EQ(radio_checks(dlg, 101) >> 3, 0x2);
  kt(VK_DOWN);
  CHECK_EQ(focus_id(dlg), 104);
  /* an edit control wants the arrow keys: the focus stays */
  SendMessage(dlg, WM_NEXTDLGCTL, itm(dlg, 110), 1);
  CHECK_EQ(focus_id(dlg), 110);
  kt(VK_DOWN);
  kt(VK_RIGHT);
  CHECK_EQ(focus_id(dlg), 110);
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- mnemonics ---- */

static void test_mnemonics(void) {
  TB t;
  HWND16 dlg;
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 200, 100, "Alt", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 10, 0xFFFF, CH, STA, "", "&Name:");
  tb_item(&t, 50, 4, 60, 12, 20, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "n");
  tb_item(&t, 4, 20, 40, 10, 0xFFFF, CH, STA, "", "&Zip:");
  tb_item(&t, 50, 20, 60, 12, 21, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "z");
  tb_item(&t, 4, 40, 40, 14, 30, CH | WS_TABSTOP, BTN, "", "&Apply");
  tb_item(&t, 50, 40, 50, 12, 31, CH | WS_TABSTOP | 3, BTN, "", "C&heck");
  tb_item(&t, 110, 40, 50, 12, 32, CH | WS_TABSTOP | WS_DISABLED, BTN, "", "&Disabled");
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  CHECK_EQ(focus_id(dlg), 20);
  /* U8: a static label passes the focus to the next tab stop */
  kt_alt('Z');
  CHECK_EQ(focus_id(dlg), 21);
  kt_alt('N');
  CHECK_EQ(focus_id(dlg), 20);
  CHECK_EQ(dl_count(WM_COMMAND), 0);
  /* a push button gets the focus and is pressed */
  kt_alt('A');
  CHECK_EQ(focus_id(dlg), 30);
  CHECK_CMDS("30");
  CHECK_EQ(dl_nth(WM_COMMAND, 0)->lp, MKL(itm(dlg, 30), BN_CLICKED));
  /* an auto check box gets the focus, is pressed (checks itself) and notifies */
  kt_alt('H');
  CHECK_EQ(focus_id(dlg), 31);
  CHECK_EQ(IsDlgButtonChecked(dlg, 31), 1);
  CHECK_CMDS("30,31");
  /* a disabled control is not a target; an unknown letter does nothing */
  kt_alt('D');
  CHECK_EQ(focus_id(dlg), 31);
  kt_alt('Q');
  CHECK_EQ(focus_id(dlg), 31);
  CHECK_CMDS("30,31");
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- dialog function result semantics ---- */

static int dp_semantics(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)wp;
  (void)lp;
  if (m == WM_USER) {
    SetWindowLong(h, DWL_MSGRESULT, 0x00012345UL);
    *ret = 1; /* handled: the answer is DWL_MSGRESULT */
    return 1;
  }
  if (m == WM_USER + 5) {
    *ret = 0; /* not handled: default processing */
    return 1;
  }
  if (m == WM_CTLCOLOR && (lp >> 16) == CTLCOLOR_BTN) {
    *ret = 0x4321; /* a brush handle is returned as it is, not through DWL_MSGRESULT */
    return 1;
  }
  if (m == WM_INITDIALOG) {
    *ret = 0xFFFF0000u | 1; /* DX garbage: only AX counts */
    return 1;
  }
  return 0;
}

static void test_result_semantics(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = dp_semantics;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK_EQ(SendMessage(dlg, WM_USER, 0, 0), 0x00012345UL);
  CHECK_EQ(GetWindowLong(dlg, DWL_MSGRESULT), 0x00012345UL);
  CHECK_EQ(SendMessage(dlg, WM_USER + 5, 0, 0), 0);
  CHECK_EQ(SendMessage(dlg, WM_CTLCOLOR, 0, MKL(itm(dlg, 1), CTLCOLOR_BTN)), 0x4321);
  /* a zero answer: the default brush (a nonzero GDI handle) */
  g_dp = NULL;
  CHECK(SendMessage(dlg, WM_CTLCOLOR, GetDC(dlg), MKL(itm(dlg, 1), CTLCOLOR_BTN)) != 0);
  /* DefDlgProc called directly with a window that has no dialog function */
  CHECK_EQ(DefDlgProc(dlg, WM_USER + 5, 0, 0), 0);
  /* DWL_USER is the application's */
  SetWindowLong(dlg, DWL_USER, 0xCAFEBABEu);
  CHECK_EQ(GetWindowLong(dlg, DWL_USER), 0xCAFEBABEu);
  /* U9: WM_CLOSE with no handler becomes IDCANCEL */
  g_dp = NULL;
  g_ndl = 0;
  SendMessage(dlg, WM_CLOSE, 0, 0);
  CHECK_CMDS("2");
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- WM_CTLCOLOR for each control kind ---- */

static void test_ctlcolor_kinds(void) {
  TB t;
  HWND16 dlg;
  unsigned mask[8] = {0}, dlgmask = 0;
  /* the CTLCOLOR_* type each control kind asks its parent for */
  static const struct {
    int id;
    unsigned want;
  } E[] = {{1, 1u << CTLCOLOR_BTN}, {2, 1u << CTLCOLOR_STATIC}, {3, 1u << CTLCOLOR_EDIT}, {4, 1u << CTLCOLOR_LISTBOX},
           {6, 1u << CTLCOLOR_SCROLLBAR}};
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 180, 120, "Colors", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 14, 1, CH | WS_TABSTOP, BTN, "", "Push");
  tb_item(&t, 50, 4, 40, 10, 2, CH, STA, "", "Text");
  tb_item(&t, 4, 24, 60, 12, 3, CH | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, EDT, "", "edit");
  tb_item(&t, 4, 40, 60, 30, 4, CH | WS_BORDER | WS_TABSTOP | LBS_NOTIFY, LST, "", "");
  tb_item(&t, 80, 24, 80, 60, 5, CH | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, CMB, "", "");
  tb_item(&t, 80, 4, 80, 10, 6, CH | SBS_HORZ, SCR, "", "");
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  dpump();
  UpdateWindow(dlg);
  dpump();
  CHECK(dl_count(WM_CTLCOLOR) > 0);
  for (int i = 0; i < g_ndl; i++) {
    unsigned type;
    HWND16 ctl;
    if (g_dl[i].msg != WM_CTLCOLOR) continue;
    type = g_dl[i].lp >> 16;
    ctl = (HWND16)(g_dl[i].lp & 0xFFFF);
    CHECK(g_dl[i].wp != 0); /* the hdc */
    CHECK(type <= CTLCOLOR_STATIC);
    if (ctl == dlg) dlgmask |= 1u << type;
    else if (GetParent(ctl) == dlg && GetWindowWord(ctl, GWL_ID) < 8) mask[GetWindowWord(ctl, GWL_ID)] |= 1u << type;
  }
  for (unsigned k = 0; k < sizeof E / sizeof E[0]; k++) {
    if (mask[E[k].id] != E[k].want)
      fprintf(stderr, "control id %d asked for CTLCOLOR mask 0x%x, want 0x%x\n", E[k].id, mask[E[k].id], E[k].want);
    CHECK_EQ(mask[E[k].id], E[k].want);
  }
  /* the dialog's own background is erased with CTLCOLOR_DLG sent to its function */
  CHECK_EQ(dlgmask, 1u << CTLCOLOR_DLG);
  /* the combo box asks for the edit and list colours of its parts, never for button / dialog ones */
  CHECK(mask[5] != 0);
  CHECK_EQ(mask[5] & ~((1u << CTLCOLOR_EDIT) | (1u << CTLCOLOR_LISTBOX)), 0);
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- focus saved and restored with the activation ---- */

static void test_focus_activation(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = dp_ret_true;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  kt(VK_TAB);
  CHECK_EQ(focus_id(dlg), 1);
  SetActiveWindow(g_top);
  CHECK_EQ(GetActiveWindow(), g_top);
  CHECK(GetFocus() != itm(dlg, 1));
  SetActiveWindow(dlg);
  CHECK_EQ(GetActiveWindow(), dlg);
  CHECK_EQ(GetFocus(), itm(dlg, 1)); /* the control that had the focus gets it back */
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- DialogBox: modal loop, owner disabled, EndDialog value ---- */

static int g_owner_enabled_at_init = -1, g_active_at_init = -1;
static HWND16 g_modal_dlg;
static int16_t g_end_value;
static int dp_modal(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)lp;
  if (m == WM_INITDIALOG) {
    g_modal_dlg = h;
    g_owner_enabled_at_init = IsWindowEnabled(g_top);
    g_active_at_init = GetActiveWindow() == h;
    *ret = 1;
    return 1;
  }
  if (m == WM_COMMAND && (wp == IDOK || wp == IDCANCEL)) {
    EndDialog(h, g_end_value);
    return 1; /* handled */
  }
  return 0;
}

static int dp_end_in_init(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)wp;
  (void)lp;
  if (m == WM_INITDIALOG) {
    EndDialog(h, 99);
    *ret = 1;
    return 1;
  }
  return 0;
}

static int dp_destroy_in_init(HWND16 h, uint16_t m, uint16_t wp, uint32_t lp, uint32_t *ret) {
  (void)wp;
  (void)lp;
  if (m == WM_INITDIALOG) {
    DestroyWindow(h);
    *ret = 1;
    return 1;
  }
  return 0;
}

/* stars-0z4.17: hiding and re-showing a dialog invalidates and paints its controls (the game toggles
 * the modeless dialog 2502 at seg31:0x5cc2). */
static void test_hide_show_repaints_controls(void) {
  HWND16 dlg;
  begin();
  g_dp = NULL;
  g_ndl = 0;
  dlg = CreateDialog(g_hinst, MK_FP(0, 2502), g_top, FP_PROC_D);
  CHECK(dlg != 0);
  CHECK_EQ(user_window_nupd(itm(dlg, 2)), 0); /* the first show (inside CreateDialog) painted the controls */
  CHECK_EQ(user_window_nupd(itm(dlg, 2503)), 0);
  ShowWindow(dlg, SW_HIDE);
  UpdateWindow(dlg);
  ValidateRect(itm(dlg, 2), 0);
  ValidateRect(itm(dlg, 2503), 0);
  CHECK_EQ(user_window_nupd(itm(dlg, 2)), 0);
  ShowWindow(dlg, SW_SHOW);
  CHECK(user_window_nupd(dlg) != 0);
  CHECK(user_window_nupd(itm(dlg, 2)) != 0);
  CHECK(user_window_nupd(itm(dlg, 2503)) != 0);
  UpdateWindow(dlg);
  UpdateWindow(itm(dlg, 2));
  UpdateWindow(itm(dlg, 2503));
  CHECK_EQ(user_window_nupd(itm(dlg, 2)), 0);
  CHECK_EQ(user_window_nupd(itm(dlg, 2503)), 0);
  DestroyWindow(dlg);
  dpump();
  end();
}

/* A dialog function that destroys the dialog from WM_INITDIALOG must not leave the owner disabled. */
static void test_destroy_in_init_reenables_owner(void) {
  int16_t r;
  begin();
  g_dp = dp_destroy_in_init;
  g_ndl = 0;
  r = DialogBox(g_hinst, MK_FP(0, 2008), g_top, FP_PROC_D);
  CHECK_EQ(r, -1);
  CHECK(IsWindowEnabled(g_top));
  CHECK_EQ(user_window_count(), 1);
  g_dp = NULL;
  end();
}

static void test_dialogbox(void) {
  int16_t r;
  begin();
  /* Template 2008 ("Player Relations": Close = id 2 is the default button and first tab stop).
   * Esc is queued before the call: the loop reads it once the dialog is up. */
  g_dp = dp_modal;
  g_ndl = 0;
  g_end_value = 7;
  rt_input_key(VK_ESCAPE, 0x01, 1, g_t += 1000);
  rt_input_key(VK_ESCAPE, 0x01, 0, g_t += 1000);
  r = DialogBox(g_hinst, MK_FP(0, 2008), g_top, FP_PROC_D);
  CHECK_EQ(r, 7); /* DialogBox returns the value passed to EndDialog */
  CHECK_EQ(g_owner_enabled_at_init, 0);   /* the owner is disabled while the dialog is up */
  CHECK_EQ(g_active_at_init, 0);          /* (not yet active at WM_INITDIALOG: it is shown afterwards) */
  CHECK(!IsWindow(g_modal_dlg));          /* destroyed */
  CHECK(IsWindowEnabled(g_top));          /* the owner works again */
  CHECK_EQ(GetActiveWindow(), g_top);
  CHECK_CMDS("2");
  CHECK_EQ(user_window_count(), 1);
  /* Enter on the focused default button, a negative result keeps its sign (INT) */
  g_ndl = 0;
  g_end_value = -3;
  rt_input_key(VK_RETURN, 0x1C, 1, g_t += 1000);
  rt_input_key(VK_RETURN, 0x1C, 0, g_t += 1000);
  r = DialogBox(g_hinst, MK_FP(0, 2008), g_top, FP_PROC_D);
  CHECK_EQ(r, -3);
  CHECK_CMDS("2");
  CHECK(IsWindowEnabled(g_top));
  /* a dialog whose function ends it from WM_INITDIALOG returns at once */
  {
    g_dp = dp_end_in_init;
    r = DialogBox(g_hinst, MK_FP(0, 2008), g_top, FP_PROC_D);
    CHECK_EQ(r, 99);
    CHECK(IsWindowEnabled(g_top));
    CHECK_EQ(user_window_count(), 1);
  }
  /* no input at all: the loop ends, the dialog is destroyed, the owner is enabled, result 0 */
  g_dp = dp_modal;
  g_end_value = 5;
  user_input_exhausted = 0;
  r = DialogBox(g_hinst, MK_FP(0, 2008), g_top, FP_PROC_D);
  CHECK_EQ(r, 0);
  CHECK(IsWindowEnabled(g_top));
  CHECK_EQ(user_window_count(), 1);
  user_input_exhausted = 0;
  /* unknown template: -1 */
  r = DialogBox(g_hinst, MK_FP(0, 31999), g_top, FP_PROC_D);
  CHECK_EQ(r, -1);
  CHECK(IsWindowEnabled(g_top));
  /* the modal frame of the template */
  end();
}

static void test_enddialog_edge(void) {
  TB t;
  HWND16 dlg;
  begin();
  edit_dialog(&t, DLG_STYLE_VIS);
  g_dp = NULL;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK_EQ(EndDialog(g_top, 1), 0);         /* not a dialog */
  CHECK_EQ(EndDialog(0xFFF0, 1), 0);        /* not a window */
  CHECK_EQ(EndDialog(dlg, 4), 1);           /* a modeless dialog is hidden, not destroyed */
  CHECK(IsWindow(dlg));
  CHECK(!IsWindowVisible(dlg));
  CHECK_EQ(EndDialog(dlg, 5), 0);           /* already ended */
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- a dropped combo box list keeps Enter for itself ---- */

static void test_dropped_combo_keeps_enter(void) {
  TB t;
  HWND16 dlg;
  begin();
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 160, 100, "Combo", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 100, 60, 5, CH | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, CMB, "", "");
  tb_item(&t, 4, 70, 36, 14, 1, CH | WS_TABSTOP | 1, BTN, "", "OK");
  g_dp = dp_ret_true;
  g_ndl = 0;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  g_pumpdlg = dlg;
  SendDlgItemMessage(dlg, 5, CB_ADDSTRING, 0, fstr("alpha"));
  SendDlgItemMessage(dlg, 5, CB_ADDSTRING, 0, fstr("beta"));
  CHECK_EQ(focus_id(dlg), 5);
  SendDlgItemMessage(dlg, 5, CB_SHOWDROPDOWN, 1, 0);
  CHECK_EQ(SendDlgItemMessage(dlg, 5, CB_GETDROPPEDSTATE, 0, 0), 1);
  kt(VK_RETURN); /* closes the list, not the dialog */
  CHECK_EQ(SendDlgItemMessage(dlg, 5, CB_GETDROPPEDSTATE, 0, 0), 0);
  CHECK_EQ(dl_count(WM_COMMAND), 0);
  kt(VK_RETURN); /* closed: the dialog's default button */
  CHECK_CMDS("1");
  g_pumpdlg = 0;
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- custom classes, failures, DS_ABSALIGN ---- */

static void test_custom_class_and_failures(void) {
  TB t;
  HWND16 dlg;
  int16_t r;
  begin();
  CHECK(register_class("MYCTL", FP_PROC_A, 0, 0) != 0);
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 100, 60, "Custom", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 12, 7, CH, 0, "MYCTL", "c");
  g_dp = NULL;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK(itm(dlg, 7) != 0);
  CHECK(strcmp(user_state()->c[user_wnd(itm(dlg, 7))->cls].name, "MYCTL") == 0);
  DestroyWindow(dlg);
  dpump();
  /* a class that is not registered: no dialog, no leftover window, the owner works */
  tb_head(&t, DLG_STYLE_VIS, 0, 0, 100, 60, "Bad", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 12, 1, CH | WS_TABSTOP, BTN, "", "OK");
  tb_item(&t, 4, 20, 40, 12, 8, CH, 0, "NOSUCHCLASS", "x");
  CHECK_EQ(user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0), 0);
  CHECK_EQ(user_window_count(), 1);
  r = user_dialog_box(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK_EQ(r, -1);
  CHECK(IsWindowEnabled(g_top));
  CHECK_EQ(user_window_count(), 1);
  /* truncated template */
  CHECK_EQ(user_dialog_create(t.b, 7, g_top, FP_PROC_D, 0), 0);
  CHECK_EQ(CreateDialog(g_hinst, MK_FP(0, 31999), g_top, FP_PROC_D), 0);
  end();
}

static void test_absalign_and_no_owner(void) {
  TB t;
  HWND16 dlg;
  RECT16 cs;
  begin();
  /* DS_ABSALIGN: x, y are screen coordinates, whatever the owner */
  tb_head(&t, DLG_STYLE_VIS | DS_ABSALIGN, 20, 16, 100, 60, "Abs", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 12, 1, CH | WS_TABSTOP, BTN, "", "OK");
  g_dp = NULL;
  dlg = user_dialog_create(t.b, t.n, g_top, FP_PROC_D, 0);
  CHECK(dlg != 0);
  CHECK(user_client_rect_screen(dlg, &cs));
  CHECK_EQ(cs.left, MulDiv(20, BASE_X, 4)); /* 25 */
  CHECK_EQ(cs.top, MulDiv(16, BASE_Y, 8));  /* 26 */
  DestroyWindow(dlg);
  dpump();
  /* no owner: screen coordinates too; a dialog taller than the room is moved up (U2) */
  tb_head(&t, DLG_STYLE_VIS, 700, 560, 100, 60, "Edge", 8, "MS Sans Serif");
  tb_item(&t, 4, 4, 40, 12, 1, CH | WS_TABSTOP, BTN, "", "OK");
  dlg = user_dialog_create(t.b, t.n, 0, FP_PROC_D, 0);
  CHECK(dlg != 0);
  {
    RECT16 wr = get_rect(dlg, 0);
    CHECK(wr.right <= GDI_SCREEN_W);
    CHECK(wr.bottom <= GDI_SCREEN_H);
    CHECK(wr.left >= 0 && wr.top >= 0);
  }
  DestroyWindow(dlg);
  dpump();
  end();
}

/* ---- main ---- */

int main(void) {
  KInitTaskRegs regs;
  (void)dump;
  if (load_exe() != 0) return 1;
  if (load_fixture() != 0) return 1;
  reg_d();
  InitTask(&regs);
  g_hinst = regs.di;
  CHECK(g_hinst != 0);
  test_constants();
  test_templates_parse();
  test_create_all_templates();
  test_dialog_units();
  test_dialog_units_rounding();
  test_item_access();
  test_init_sequence();
  test_dialog_without_font_and_frame();
  test_tab_order();
  test_modeless_without_isdialogmessage();
  test_enter_esc_default_button();
  test_arrows_group();
  test_mnemonics();
  test_result_semantics();
  test_ctlcolor_kinds();
  test_focus_activation();
  test_dialogbox();
  test_hide_show_repaints_controls();
  test_destroy_in_init_reenables_owner();
  test_enddialog_edge();
  test_dropped_combo_keeps_enter();
  test_custom_class_and_failures();
  test_absalign_and_no_owner();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  free(g_exe);
  return TEST_DONE();
}
