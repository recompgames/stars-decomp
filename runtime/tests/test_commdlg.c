/* Expectations for runtime/commdlg.c: GetOpenFileName, GetSaveFileName, PrintDlg (stub) and
 * CommDlgExtendedError. Task stars-0z4.14, written BEFORE the implementation. Do not edit an
 * expectation to match the code: a disagreement is reported with both values.
 *
 * FACTS FROM THE GAME (re-derived from original/stars26i.exe by reading the code bytes of the call
 * sites and the string decoder 8:0x8df4; docs/inventory.md section 19):
 *   site 1  4:0x3b66  GetOpenFileName. Filter = string 0x306 "Stars! Game Files|*.R*|All Files|*.*|"
 *           when the second argument is non-zero (open a race), else string 0x307
 *           "Stars! Game Files|*.M*;*.HST;*.R*|Stars! Host Files|*.HST|All Files|*.*|"; '|' is
 *           turned into NUL by the game. nFilterIndex 1, lpstrFile (256) starting with NUL,
 *           lpstrFileTitle 256, lpstrInitialDir = DS:0x212, no title, no default extension,
 *           Flags 0x1804 = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST.
 *   site 2  15:0x7868 GetSaveFileName. Filter string 0x305 "Stars! Game Files|*.XY|", lpstrFile =
 *           copy of the caller's default name (256), lpstrInitialDir = DS:0x212, lpstrTitle =
 *           DS:0x0A96 "Choose New Game Name", lpstrDefExt "xy", Flags 0x8806 = OFN_NOREADONLYRETURN |
 *           OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT. The game reads nFileExtension
 *           (+0x36): non-zero -> it overwrites the extension with "xy", else appends it; then it
 *           copies lpstrFile to the caller's buffer.
 *   site 3  28:0x39d8 GetSaveFileName. Filter = DS:0x14FB "Stars! Race Files|*.R*|", lpstrFile =
 *           copy of the default name, no initial dir, no title, lpstrDefExt = DS:0x1513 "r1", Flags
 *           0x8806. The game reads nFileOffset (+0x34) and copies the BARE name ("MINE.R1") to DS:0x1444.
 *   PrintDlg 4:0x1f40: PRINTDLG with lStructSize 0x34, Flags 0x500 = PD_RETURNDC | PD_RETURNDEFAULT.
 * The strings 0x305..0x307 are decoded here from the game's own tables (segment 8, nibble coded).
 *
 * UNVERIFIED (the Windows 3.1 SDK text is not on this machine and tests/ref/ holds no capture; every
 * item is also in docs/inventory.md section 19 and in runtime/commdlg.c):
 *   C1  File Name edit starts with lpstrFile, else with the pattern of the selected filter; names in
 *       the lists are lower case; the returned path is upper case (DOS canonical form).
 *   C2  Selecting a file name in the list copies it to the edit; double click = OK; selecting a
 *       filter re-lists the files and puts its pattern in the edit; a pattern typed in the edit
 *       (wild cards) re-lists the files and keeps the dialog open; a directory typed there
 *       changes the directory.
 *   C3  Directory list: "c:\", every directory down to the current one, then the sub-directories
 *       (sorted); a double click on an entry changes to it. No folder icons.
 *   C4  The dialog changes the current directory to the one it ends in, on OK and Cancel, unless
 *       OFN_NOCHANGEDIR.
 *   C5  Default extension appended when the name has no '.'; OFN_EXTENSIONDIFFERENT when the typed
 *       extension differs from lpstrDefExt. nFileExtension = offset of the terminating NUL when
 *       there is no extension; 0 for a name ending in '.' (UNVERIFIED: Wine's PathFindExtension+1 rule
 *       would keep the dot and point at the NUL).
 *   C6  Error / confirmation texts and which message box stops the dialog (a path or file that must
 *       exist but does not, a read-only file with OFN_NOREADONLYRETURN, an existing file with
 *       OFN_OVERWRITEPROMPT answered No) are from memory.
 *   C7  Buffer too small: FALSE, CommDlgExtendedError = FNERR_BUFFERTOOSMALL (0x3003), the first
 *       word of lpstrFile = the length needed including the NUL.
 *   C8  OFN field offsets other than the ones the game stores (see commdlg.h) and the dialog
 *       layout (no capture).
 */
#include "test.h"

#include <stddef.h>

#include "ctl2_test_common.h"
#include "runtime/commdlg.h"
#include "harness/commdlg_thunks.h"
#include "runtime/dialog.h"

/* ---- OPENFILENAME layout ---- */

#define OFS(f, n) _Static_assert(offsetof(OPENFILENAME16, f) == (n), "OPENFILENAME16." #f)
OFS(lStructSize, 0x00);
OFS(hwndOwner, 0x04);
OFS(hInstance, 0x06);
OFS(lpstrFilter, 0x08);
OFS(lpstrCustomFilter, 0x0C);
OFS(nMaxCustFilter, 0x10);
OFS(nFilterIndex, 0x14);
OFS(lpstrFile, 0x18);
OFS(nMaxFile, 0x1C);
OFS(lpstrFileTitle, 0x20);
OFS(nMaxFileTitle, 0x24);
OFS(lpstrInitialDir, 0x28);
OFS(lpstrTitle, 0x2C);
OFS(Flags, 0x30);
OFS(nFileOffset, 0x34);
OFS(nFileExtension, 0x36);
OFS(lpstrDefExt, 0x38);
OFS(lCustData, 0x3C);
OFS(lpfnHook, 0x40);
OFS(lpTemplateName, 0x44);
_Static_assert(sizeof(OPENFILENAME16) == 0x48, "OPENFILENAME16 size");

/* ---- the game's strings, decoded from the image ---- */

/* Segment 8 starts at file offset 0x22DC0 (build/nedump --segments); the decoder 8:0x8DF4 reads the
 * code-segment tables CS:0x8C (word offsets per block of 64), CS:0xB6 (lengths), CS:0x38 (symbols)
 * and the nibble stream at CS:0x5D7. */
static void dec_str(unsigned id, char *out) {
  const uint8_t *d = g_img + 0x22DC0;
  const uint8_t *lens = d + 0xB6 + (id >> 6) * 0x40;
  unsigned k = id & 0x3F, sum = 0, acc = 0, n, even, p;
  for (unsigned i = 0; i < k; i++) sum += lens[i];
  p = (unsigned)(d[0x8C + (id >> 6) * 2] | (d[0x8D + (id >> 6) * 2] << 8)) + 0x5D7 + (sum >> 1);
  n = lens[k];
  even = (sum & 1) == 0;
  for (unsigned i = 0; i < n; i++) {
    unsigned v;
    if (even) v = d[p] >> 4;
    else v = d[p++] & 15;
    even = !even;
    acc += v;
    if (v != 15) {
      *out++ = (char)d[0x38 + acc];
      acc = 0;
    }
  }
  *out = 0;
}
static const char *dgroup_str(unsigned off) { return (const char *)(g_img + 0x54E40 + off); }

/* ---- helpers ---- */

static FARPTR fbuf(const char *s, size_t cap) {
  FARPTR p = far_alloc((uint32_t)cap);
  snprintf((char *)LIN(p), cap, "%s", s ? s : "");
  return p;
}
/* "a|b|" -> "a\0b\0\0" as the game does */
static FARPTR filter_fp(const char *bar) {
  size_t n = strlen(bar);
  FARPTR p = far_alloc((uint32_t)n + 2);
  char *q = LIN(p);
  memcpy(q, bar, n + 1);
  for (size_t i = 0; i < n; i++)
    if (q[i] == '|') q[i] = 0;
  q[n + 1] = 0;
  return p;
}

typedef struct Cfg {
  uint32_t flags;
  const char *filter; /* with '|' */
  const char *file;   /* initial lpstrFile */
  const char *dir;    /* lpstrInitialDir, NULL = none */
  const char *title;  /* NULL = none */
  const char *defext; /* NULL = none */
  uint32_t maxfile;   /* 0 = 256 */
} Cfg;

static FARPTR g_ofn_fp;
static OPENFILENAME16 *O(void) { return LIN(g_ofn_fp); }
static const char *file_of(void) { return LIN(O()->lpstrFile); }
static const char *ftitle_of(void) { return LIN(O()->lpstrFileTitle); }

static void mk_ofn(const Cfg *c) {
  OPENFILENAME16 *o;
  g_ofn_fp = far_alloc(sizeof(OPENFILENAME16));
  o = LIN(g_ofn_fp);
  memset(o, 0, sizeof *o);
  o->lStructSize = 0x48;
  o->hwndOwner = g_top;
  o->lpstrFilter = c->filter ? filter_fp(c->filter) : 0;
  o->nFilterIndex = 1;
  o->nMaxFile = c->maxfile ? c->maxfile : 256;
  o->lpstrFile = fbuf(c->file, 256);
  o->lpstrFileTitle = fbuf("", 256);
  o->nMaxFileTitle = 256;
  o->lpstrInitialDir = c->dir ? fbuf(c->dir, 80) : 0;
  o->lpstrTitle = c->title ? fbuf(c->title, 80) : 0;
  o->lpstrDefExt = c->defext ? fbuf(c->defext, 8) : 0;
  o->Flags = c->flags;
}

/* the three call sites */
static Cfg site1(int open_race) {
  static char f[256];
  Cfg c = {0x1804, NULL, "", "", NULL, NULL, 0};
  dec_str(open_race ? 0x306 : 0x307, f);
  c.filter = f;
  return c;
}
static Cfg site2(const char *deffile) {
  static char f[256];
  Cfg c = {0x8806, NULL, deffile, "", NULL, "xy", 0};
  dec_str(0x305, f);
  c.filter = f;
  c.title = dgroup_str(0x0A96);
  return c;
}
static Cfg site3(const char *deffile) {
  Cfg c = {0x8806, NULL, deffile, NULL, NULL, NULL, 0};
  c.filter = dgroup_str(0x14FB);
  c.defext = dgroup_str(0x1513);
  return c;
}

static void world(void) {
  vfs_init();
  vfs_put("C:\\STARS\\TEST.M1", "m", 1);
  vfs_put("C:\\STARS\\GAME.HST", "h", 1);
  vfs_put("C:\\STARS\\GAME.XY", "x", 1);
  vfs_put("C:\\STARS\\MYRACE.R1", "r", 1);
  vfs_put("C:\\STARS\\README.TXT", "t", 1);
  vfs_mkdir("C:\\STARS\\SUB");
  vfs_mkdir("C:\\STARS\\DATA");
}

/* ---- scripted input ---- */

typedef struct Step {
  char k;
  int a;
  const char *s;
  void (*fn)(void);
} Step;
static const Step *g_steps;
static int g_istep, g_nsteps;

static HWND16 DLG(void) { return commdlg_active_dialog(); }
static HWND16 ctl(int id) { return GetDlgItem(DLG(), (int16_t)id); }
static void mouse_down_up(int x, int y) {
  g_t += 1000;
  rt_input_mouse((int16_t)x, (int16_t)y, 1, g_t);
  rt_input_mouse((int16_t)x, (int16_t)y, 0, g_t + 50);
}
static void dbl(int x, int y) {
  g_t += 1000;
  rt_input_mouse((int16_t)x, (int16_t)y, 1, g_t);
  rt_input_mouse((int16_t)x, (int16_t)y, 0, g_t + 20);
  rt_input_mouse((int16_t)x, (int16_t)y, 1, g_t + 40);
  rt_input_mouse((int16_t)x, (int16_t)y, 0, g_t + 60);
}
static void center_of(HWND16 h, int *x, int *y) {
  RECT16 r = get_rect(h, 0);
  *x = (r.left + r.right) / 2;
  *y = (r.top + r.bottom) / 2;
}
static int find_item(HWND16 lb, const char *name) {
  return (int)(int16_t)SM(lb, LB_FINDSTRINGEXACT, 0xFFFF, fstr(name));
}
static void item_center(HWND16 lb, int idx, int *x, int *y) {
  FARPTR p = far_alloc(sizeof(RECT16));
  RECT16 *r;
  POINT16 a;
  SM(lb, LB_GETITEMRECT, (uint16_t)idx, p);
  r = LIN(p);
  a = c2s(lb, (r->left + r->right) / 2, (r->top + r->bottom) / 2);
  *x = a.x;
  *y = a.y;
}
static int msgbox_up(void) {
  HWND16 h;
  int n, def;
  RECT16 b[3], ic, tx;
  uint16_t ids[3];
  return user_msgbox_info(&h, &n, b, ids, &def, &ic, &tx);
}
static void mb_press(int id) {
  HWND16 h;
  int n, def;
  RECT16 b[3], ic, tx;
  uint16_t ids[3];
  n = 0;
  CHECK(user_msgbox_info(&h, &n, b, ids, &def, &ic, &tx));
  for (int i = 0; i < n && i < 3; i++)
    if (ids[i] == id) mouse_down_up((b[i].left + b[i].right) / 2, (b[i].top + b[i].bottom) / 2);
}

static void tap_raw(unsigned vk) { /* queue only: the modal loop (IsDialogMessage) must see the keys */
  g_t += 1000;
  rt_input_key((uint16_t)vk, 0x10, 1, g_t);
  g_t += 1000;
  rt_input_key((uint16_t)vk, 0x10, 0, g_t);
}

static bool hook(void) {
  const Step *s;
  int x, y;
  if (g_istep >= g_nsteps) return false;
  s = &g_steps[g_istep++];
  g_t += 1000;
  switch (s->k) {
    case 'T': SetDlgItemText(DLG(), (int16_t)s->a, fstr(s->s)); break;           /* set an edit text */
    case 'K': tap_raw((unsigned)s->a); break; /* tap a key */
    case 'Y':                                                                    /* type a string */
      for (const char *q = s->s; *q; q++) {
        unsigned vk = (unsigned)(*q >= 'a' && *q <= 'z' ? *q - 32 : *q);
        if (*q == '.') vk = 0xBE;
        if (*q == '*') vk = 0x6A;
        tap_raw(vk);
      }
      break;
    case 'C':                                                                    /* click a control */
      center_of(ctl(s->a), &x, &y);
      mouse_down_up(x, y);
      break;
    case 'L':                                                                    /* click a list item by name */
      item_center(ctl(s->a), find_item(ctl(s->a), s->s), &x, &y);
      mouse_down_up(x, y);
      break;
    case 'D':                                                                    /* double click a list item */
      item_center(ctl(s->a), find_item(ctl(s->a), s->s), &x, &y);
      dbl(x, y);
      break;
    case 'B': mb_press(s->a); break;
    case 'F': s->fn(); break;
    default: break;
  }
  return true;
}

static uint16_t run_dialog(const Step *steps, int n, int save) {
  uint16_t r;
  g_steps = steps;
  g_nsteps = n;
  g_istep = 0;
  user_wait_hook = hook;
  user_input_exhausted = 0;
  r = save ? GetSaveFileName(g_ofn_fp) : GetOpenFileName(g_ofn_fp);
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  CHECK_EQ(g_istep, g_nsteps); /* every scripted step was consumed */
  return r;
}
#define RUN(steps, save) run_dialog((steps), (int)(sizeof(steps) / sizeof((steps)[0])), (save))

static void select_combo(int id, int idx) {
  HWND16 cb = ctl(id);
  SM(cb, CB_SETCURSEL, (uint16_t)idx, 0);
  SendMessage(DLG(), WM_COMMAND, (uint16_t)id, MKL(cb, CBN_SELCHANGE));
}
static void sel_filter1(void) { select_combo(CD_cmb1, 1); }
static void sel_filter2(void) { select_combo(CD_cmb1, 2); }

static const char *lb_items(int id) {
  static char out[4][512];
  static int k;
  HWND16 lb = ctl(id);
  int n = (int)(int16_t)SM(lb, LB_GETCOUNT, 0, 0);
  size_t len = 0;
  k = (k + 1) & 3;
  out[k][0] = 0;
  for (int i = 0; i < n; i++)
    len += (size_t)snprintf(out[k] + len, sizeof out[k] - len, "%s%s", i ? "," : "", item_text(lb, LB_GETTEXT, i));
  return out[k];
}
static const char *cb_items(int id) {
  static char out[4][512];
  static int k;
  HWND16 cb = ctl(id);
  int n = (int)(int16_t)SM(cb, CB_GETCOUNT, 0, 0);
  size_t len = 0;
  k = (k + 1) & 3;
  out[k][0] = 0;
  for (int i = 0; i < n; i++)
    len += (size_t)snprintf(out[k] + len, sizeof out[k] - len, "%s%s", i ? "|" : "", item_text(cb, CB_GETLBTEXT, i));
  return out[k];
}
/* snapshot strings taken while the dialog is up */
static char g_s[8][256];
static void snap(int i, const char *v) { snprintf(g_s[i], sizeof g_s[i], "%s", v); }

/* ---- tests ---- */

static void test_game_strings(void) {
  char s[256];
  dec_str(0x305, s);
  CHECK(strcmp(s, "Stars! Game Files|*.XY|") == 0);
  dec_str(0x306, s);
  CHECK(strcmp(s, "Stars! Game Files|*.R*|All Files|*.*|") == 0);
  dec_str(0x307, s);
  CHECK(strcmp(s, "Stars! Game Files|*.M*;*.HST;*.R*|Stars! Host Files|*.HST|All Files|*.*|") == 0);
  CHECK(strcmp(dgroup_str(0x14FB), "Stars! Race Files|*.R*|") == 0);
  CHECK(strcmp(dgroup_str(0x1513), "r1") == 0);
  CHECK(strcmp(dgroup_str(0x0A96), "Choose New Game Name") == 0);
  /* the three call-site flag words */
  CHECK_EQ(OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST, 0x1804);
  CHECK_EQ(OFN_NOREADONLYRETURN | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT, 0x8806);
  CHECK_EQ(PD_RETURNDC | PD_RETURNDEFAULT, 0x500);
}

/* the layout facts recorded while the File Open dialog (site 1, open a game) is up */
static void snap_open(void) {
  snap(0, lb_items(CD_lst1));
  snap(1, cb_items(CD_cmb1));
  snap(2, text_of(ctl(CD_edt1)));
  snap(3, text_of(ctl(CD_stc1)));
  snap(4, lb_items(CD_lst2));
  snap(5, cb_items(CD_cmb2));
  snap(6, text_of(DLG()));
  CHECK_EQ((int)(int16_t)SM(ctl(CD_cmb1), CB_GETCURSEL, 0, 0), 0);
}
static void layout_open(void) {
  RECT16 dlg = get_rect(DLG(), 0), lst1 = get_rect(ctl(CD_lst1), 0), lst2 = get_rect(ctl(CD_lst2), 0),
         edt = get_rect(ctl(CD_edt1), 0), ok = get_rect(ctl(IDOK), 0), cancel = get_rect(ctl(IDCANCEL), 0),
         cmb1 = get_rect(ctl(CD_cmb1), 0), cmb2 = get_rect(ctl(CD_cmb2), 0), stc = get_rect(ctl(CD_stc1), 0);
  PIXEL_TEST("commdlg_fileopen");
  /* structure of the Windows 3.1 File Open dialog: file name column left, directories column
   * right of it, the buttons at the far right; the type combo under the file list, the drives
   * combo under the directory list (UNVERIFIED: no reference capture) */
  CHECK(lst1.right <= lst2.left);
  CHECK(lst2.right <= ok.left);
  CHECK(ok.bottom <= cancel.top);
  CHECK_EQ(ok.left, cancel.left);
  CHECK(edt.bottom <= lst1.top);
  CHECK(lst1.bottom <= cmb1.top);
  CHECK(lst2.bottom <= cmb2.top);
  CHECK_EQ(cmb1.top, cmb2.top);
  CHECK(stc.bottom <= lst2.top);
  CHECK(stc.left >= lst1.right);
  CHECK(dlg.left >= 0 && dlg.top >= 0 && dlg.right <= 800 && dlg.bottom <= 600);
  CHECK(cancel.right <= dlg.right && cmb1.bottom <= dlg.bottom);
  CHECK(IsWindowVisible(DLG()));
  CHECK_EQ(ctl(CD_chx1), 0); /* OFN_HIDEREADONLY: no Read Only check box */
  dump_frame("commdlg_fileopen", dlg.left, dlg.top, dlg.right - dlg.left, dlg.bottom - dlg.top);
}

static void snap_edit_click(void) { snap(7, text_of(ctl(CD_edt1))); }

static void test_open_click(void) {
  static const Step st[] = {{'F', 0, 0, snap_open},
                            {'F', 0, 0, layout_open},
                            {'L', CD_lst1, "test.m1", 0},
                            {'F', 0, 0, snap_edit_click},
                            {'C', IDOK, 0, 0}};
  Cfg c = site1(0);
  uint16_t r;
  begin();
  world();
  mk_ofn(&c);
  r = RUN(st, 0);
  CHECK_EQ(r, 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\TEST.M1") == 0);
  CHECK(strcmp(ftitle_of(), "TEST.M1") == 0);
  CHECK_EQ(O()->nFileOffset, 9);
  CHECK_EQ(O()->nFileExtension, 14);
  CHECK_EQ(O()->nFilterIndex, 1);
  CHECK_EQ(O()->Flags & 0xFFFF, 0x1804);
  CHECK_EQ(CommDlgExtendedError(), 0);
  /* what the dialog showed */
  CHECK(strcmp(g_s[0], "game.hst,myrace.r1,test.m1") == 0);
  CHECK(strcmp(g_s[1], "Stars! Game Files|Stars! Host Files|All Files") == 0);
  CHECK(strcmp(g_s[2], "*.M*;*.HST;*.R*") == 0);
  CHECK(strcmp(g_s[3], "c:\\stars") == 0);
  CHECK(strcmp(g_s[4], "c:\\,stars,data,sub") == 0);
  CHECK(strcmp(g_s[5], "c:") == 0);
  CHECK(strcmp(g_s[6], "Open") == 0);
  CHECK(strcmp(g_s[7], "test.m1") == 0);
  CHECK_EQ(user_window_count(), 1); /* only the owner is left */
  CHECK(IsWindowEnabled(g_top));
  end();
}


static void test_open_edit_follows_list(void) {
  static const Step st[] = {{'L', CD_lst1, "myrace.r1", 0}, {'F', 0, 0, snap_edit_click}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[7], "myrace.r1") == 0); /* C2 */
  CHECK_EQ(CommDlgExtendedError(), 0);
  end();
}

static void test_cancel(void) {
  static const Step st1[] = {{'C', IDCANCEL, 0, 0}};
  static const Step st2[] = {{'K', VK_ESCAPE, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  strcpy(LIN(O()->lpstrFile), "");
  CHECK_EQ(RUN(st1, 0), 0);
  CHECK_EQ(CommDlgExtendedError(), 0);
  CHECK(strcmp(file_of(), "") == 0); /* untouched on Cancel */
  CHECK(IsWindowEnabled(g_top));
  mk_ofn(&c);
  CHECK_EQ(RUN(st2, 0), 0); /* Esc = Cancel */
  CHECK_EQ(user_window_count(), 1);
  /* no input at all: the loop ends without a choice */
  mk_ofn(&c);
  g_nsteps = 0;
  user_input_exhausted = 0;
  CHECK_EQ(GetOpenFileName(g_ofn_fp), 0);
  user_input_exhausted = 0;
  end();
}

static void snap_f1(void) { snap(0, lb_items(CD_lst1)); snap(1, text_of(ctl(CD_edt1))); }
static void snap_f2(void) { snap(2, lb_items(CD_lst1)); snap(3, text_of(ctl(CD_edt1))); }
static void test_filter_combo(void) {
  static const Step st[] = {{'F', 0, 0, sel_filter1},  {'F', 0, 0, snap_f1}, {'F', 0, 0, sel_filter2},
                            {'F', 0, 0, snap_f2},      {'L', CD_lst1, "readme.txt", 0}, {'C', IDOK, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 1);
  CHECK(strcmp(g_s[0], "game.hst") == 0);
  CHECK(strcmp(g_s[1], "*.HST") == 0);
  CHECK(strcmp(g_s[2], "game.hst,game.xy,myrace.r1,readme.txt,test.m1") == 0); /* "*.*": files only, no directories */
  CHECK(strcmp(g_s[3], "*.*") == 0);
  CHECK(strcmp(file_of(), "C:\\STARS\\README.TXT") == 0);
  CHECK_EQ(O()->nFilterIndex, 3); /* the selected filter comes back */
  CHECK_EQ(O()->nFileExtension, 16); /* "C:\\STARS\\README.TXT": README is 6 characters, '.' at 15 */
  end();
}

static void test_open_race_filter(void) {
  static const Step st[] = {{'F', 0, 0, snap_f1}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(1); /* string 0x306: races + all files */
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[0], "myrace.r1") == 0);
  CHECK(strcmp(g_s[1], "*.R*") == 0);
  end();
}

static void test_typed_name(void) {
  static const Step st[] = {{'T', CD_edt1, "game.hst", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\GAME.HST") == 0);
  CHECK_EQ(O()->nFileOffset, 9);
  CHECK_EQ(O()->nFileExtension, 14);
  end();
}

static void test_typed_with_keys(void) {
  static const Step st[] = {{'K', 0x2E /* Delete: clear the selected pattern */, 0, 0},
                            {'Y', 0, "game.hst", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\GAME.HST") == 0);
  CHECK(strcmp(ftitle_of(), "GAME.HST") == 0);
  end();
}

static void test_double_click_accepts(void) {
  static const Step st[] = {{'D', CD_lst1, "test.m1", 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\TEST.M1") == 0);
  end();
}

static void snap_mb(void) { snap(0, msgbox_up() ? "box" : "none"); }
static void snap_dlg_alive(void) { snap(1, DLG() && IsWindow(DLG()) ? "alive" : "gone"); }

static void test_filemustexist(void) {
  static const Step st[] = {{'T', CD_edt1, "nofile.m1", 0}, {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_mb},
                            {'B', IDOK, 0, 0},              {'F', 0, 0, snap_dlg_alive}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[0], "box") == 0);    /* C6: a message box tells the file is missing */
  CHECK(strcmp(g_s[1], "alive") == 0);  /* and the dialog stays */
  /* without OFN_FILEMUSTEXIST a new name is accepted (OFN_PATHMUSTEXIST only) */
  {
    static const Step st2[] = {{'T', CD_edt1, "nofile.m1", 0}, {'K', VK_RETURN, 0, 0}};
    c.flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    mk_ofn(&c);
    CHECK_EQ(RUN(st2, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\NOFILE.M1") == 0);
  }
  /* a directory is not a file: OFN_FILEMUSTEXIST refuses it by navigating instead */
  end();
}

static void test_pathmustexist(void) {
  static const Step st[] = {{'T', CD_edt1, "C:\\NOPE\\X.M1", 0}, {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_mb},
                            {'B', IDOK, 0, 0},                   {'F', 0, 0, snap_dlg_alive}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[0], "box") == 0);
  CHECK(strcmp(g_s[1], "alive") == 0);
  end();
}

static void snap_dirs(void) {
  snap(0, text_of(ctl(CD_stc1)));
  snap(1, lb_items(CD_lst1));
  snap(2, lb_items(CD_lst2));
}
static void snap_dirs2(void) {
  snap(3, text_of(ctl(CD_stc1)));
  snap(4, lb_items(CD_lst2));
}
static void snap_dirs3(void) {
  snap(5, text_of(ctl(CD_stc1)));
  snap(6, lb_items(CD_lst2));
}
static void test_directories(void) {
  static const Step st[] = {{'D', CD_lst2, "sub", 0}, {'F', 0, 0, snap_dirs},
                            {'D', CD_lst2, "c:\\", 0}, {'F', 0, 0, snap_dirs2},
                            {'T', CD_edt1, "C:\\WINDOWS", 0}, {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_dirs3},
                            {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(0);
  char cwd[VFS_PATH_MAX];
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[0], "c:\\stars\\sub") == 0);
  CHECK(strcmp(g_s[1], "") == 0);                       /* nothing in SUB matches *.M*;*.HST;*.R* */
  CHECK(strcmp(g_s[2], "c:\\,stars,sub") == 0);         /* C3 */
  CHECK(strcmp(g_s[3], "c:\\") == 0);
  CHECK(strcmp(g_s[4], "c:\\,stars,windows") == 0);
  CHECK(strcmp(g_s[5], "c:\\windows") == 0);            /* a directory typed in the edit */
  CHECK(strcmp(g_s[6], "c:\\,windows") == 0);
  vfs_getcwd(cwd);
  CHECK(strcmp(cwd, "C:\\WINDOWS") == 0);               /* C4: the dialog leaves the current directory where it ended */
  /* OFN_NOCHANGEDIR restores it */
  {
    static const Step st2[] = {{'D', CD_lst2, "sub", 0}, {'C', IDCANCEL, 0, 0}};
    vfs_chdir("C:\\STARS");
    c.flags |= OFN_NOCHANGEDIR;
    mk_ofn(&c);
    CHECK_EQ(RUN(st2, 0), 0);
    vfs_getcwd(cwd);
    CHECK(strcmp(cwd, "C:\\STARS") == 0);
  }
  /* OFN_NOCHANGEDIR off: accepting a file in SUB moves the current directory there */
  {
    static const Step st3[] = {{'D', CD_lst2, "sub", 0}, {'T', CD_edt1, "new.m1", 0}, {'K', VK_RETURN, 0, 0}};
    vfs_chdir("C:\\STARS");
    c.flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    mk_ofn(&c);
    CHECK_EQ(RUN(st3, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\SUB\\NEW.M1") == 0);
    vfs_getcwd(cwd);
    CHECK(strcmp(cwd, "C:\\STARS\\SUB") == 0);
  }
  end();
}

static void test_typed_path_changes_dir(void) {
  char cwd[VFS_PATH_MAX];
  Cfg c = site1(0);
  /* absolute typed path: the game opens the BARE name relative to the current directory */
  {
    static const Step st[] = {{'T', CD_edt1, "C:\\STARS\\SUB\\X.M1", 0}, {'K', VK_RETURN, 0, 0}};
    begin();
    world();
    vfs_put("C:\\STARS\\SUB\\X.M1", "m", 1);
    vfs_chdir("C:\\STARS");
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\SUB\\X.M1") == 0);
    CHECK_EQ(O()->nFileOffset, 13);
    vfs_getcwd(cwd);
    CHECK(strcmp(cwd, "C:\\STARS\\SUB") == 0);
    end();
  }
  /* relative typed path */
  {
    static const Step st[] = {{'T', CD_edt1, "SUB\\X.M1", 0}, {'K', VK_RETURN, 0, 0}};
    begin();
    world();
    vfs_put("C:\\STARS\\SUB\\X.M1", "m", 1);
    vfs_chdir("C:\\STARS");
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\SUB\\X.M1") == 0);
    CHECK_EQ(O()->nFileOffset, 13);
    vfs_getcwd(cwd);
    CHECK(strcmp(cwd, "C:\\STARS\\SUB") == 0);
    end();
  }
  /* OFN_NOCHANGEDIR keeps the original directory */
  {
    static const Step st[] = {{'T', CD_edt1, "C:\\STARS\\SUB\\X.M1", 0}, {'K', VK_RETURN, 0, 0}};
    begin();
    world();
    vfs_put("C:\\STARS\\SUB\\X.M1", "m", 1);
    vfs_chdir("C:\\STARS");
    c.flags |= OFN_NOCHANGEDIR;
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\SUB\\X.M1") == 0);
    vfs_getcwd(cwd);
    CHECK(strcmp(cwd, "C:\\STARS") == 0);
    end();
  }
}

static void test_wildcard_in_edit(void) {
  static const Step st[] = {{'T', CD_edt1, "*.TXT", 0}, {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_dirs},
                            {'C', IDCANCEL, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK(strcmp(g_s[1], "readme.txt") == 0); /* C2: re-listed, dialog open */
  end();
}

static void test_save_game(void) {
  static const Step st[] = {{'F', 0, 0, snap_open}, {'T', CD_edt1, "newgame", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site2("");
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\NEWGAME.XY") == 0); /* default extension "xy" appended (C5) */
  CHECK(strcmp(ftitle_of(), "NEWGAME.XY") == 0);
  CHECK_EQ(O()->nFileOffset, 9);
  CHECK_EQ(O()->nFileExtension, 17);
  /* site 2 reads nFileExtension: non-zero -> overwrite the extension with "xy", else append */
  CHECK(strcmp((const char *)LIN(O()->lpstrFile) + O()->nFileExtension, "XY") == 0);
  CHECK_EQ(O()->Flags & OFN_EXTENSIONDIFFERENT, 0);
  CHECK(strcmp(g_s[6], "Choose New Game Name") == 0); /* lpstrTitle */
  CHECK(strcmp(g_s[1], "Stars! Game Files") == 0);
  CHECK(strcmp(g_s[0], "game.xy") == 0);
  CHECK(strcmp(g_s[2], "*.XY") == 0);
  end();
}

static void test_save_race_site3(void) {
  static const Step st[] = {{'F', 0, 0, snap_open}, {'T', CD_edt1, "mine", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site3("");
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\MINE.R1") == 0);
  /* site 3 reads nFileOffset and copies the BARE file name into DS:0x1444 */
  CHECK(strcmp((const char *)LIN(O()->lpstrFile) + O()->nFileOffset, "MINE.R1") == 0);
  CHECK(strcmp((const char *)LIN(O()->lpstrFile) + O()->nFileExtension, "R1") == 0);
  CHECK(strcmp(g_s[6], "Save As") == 0); /* no lpstrTitle: the default title (C6) */
  CHECK(strcmp(g_s[0], "myrace.r1") == 0);
  end();
}

static void test_extension_cases(void) {
  Cfg c = site3("");
  begin();
  world();
  {
    static const Step st[] = {{'T', CD_edt1, "mine.ab", 0}, {'K', VK_RETURN, 0, 0}};
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 1), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\MINE.AB") == 0); /* a typed extension is kept */
    CHECK(O()->Flags & OFN_EXTENSIONDIFFERENT);
    CHECK_EQ(O()->nFileExtension, 14);
  }
  {
    static const Step st[] = {{'T', CD_edt1, "mine.R1", 0}, {'K', VK_RETURN, 0, 0}};
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 1), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\MINE.R1") == 0);
    CHECK_EQ(O()->Flags & OFN_EXTENSIONDIFFERENT, 0); /* same extension, case ignored */
  }
  {
    static const Step st[] = {{'T', CD_edt1, "mine.", 0}, {'K', VK_RETURN, 0, 0}};
    mk_ofn(&c);
    CHECK_EQ(RUN(st, 1), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\MINE.") == 0 || strcmp(file_of(), "C:\\STARS\\MINE") == 0); /* C5: no default extension */
    CHECK_EQ(O()->nFileExtension, 0);
  }
  {
    /* no default extension: nFileExtension is the offset of the terminating NUL */
    static const Step st[] = {{'T', CD_edt1, "plain", 0}, {'K', VK_RETURN, 0, 0}};
    Cfg d = site1(0);
    d.flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    mk_ofn(&d);
    CHECK_EQ(RUN(st, 0), 1);
    CHECK(strcmp(file_of(), "C:\\STARS\\PLAIN") == 0);
    CHECK_EQ(O()->nFileExtension, 14);
  }
  end();
}

static void test_overwrite_prompt(void) {
  static const Step st[] = {{'F', 0, 0, snap_open}, /* the default name is in the edit */
                            {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_mb}, {'B', IDNO, 0, 0},
                            {'F', 0, 0, snap_dlg_alive}, {'K', VK_RETURN, 0, 0}, {'B', IDYES, 0, 0}};
  Cfg c = site2("GAME.XY");
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 1);
  CHECK(strcmp(g_s[2], "GAME.XY") == 0);   /* the default name is in the edit */
  CHECK(strcmp(g_s[0], "box") == 0);        /* OFN_OVERWRITEPROMPT asked */
  CHECK(strcmp(g_s[1], "alive") == 0);      /* No keeps the dialog open */
  CHECK(strcmp(file_of(), "C:\\STARS\\GAME.XY") == 0);
  end();
}

static void test_overwrite_no_flag(void) {
  /* without OFN_OVERWRITEPROMPT an existing file is returned at once */
  static const Step st[] = {{'T', CD_edt1, "game.xy", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site2("");
  begin();
  world();
  c.flags &= ~OFN_OVERWRITEPROMPT;
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 1);
  CHECK(strcmp(file_of(), "C:\\STARS\\GAME.XY") == 0);
  end();
}

static void test_overwrite_no_answer_cancels(void) {
  static const Step st[] = {{'T', CD_edt1, "game.xy", 0}, {'K', VK_RETURN, 0, 0}, {'B', IDNO, 0, 0}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site2("");
  begin();
  world();
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 0);
  CHECK(strcmp(file_of(), "") == 0);
  end();
}

static void test_readonly_save(void) {
  static const Step st[] = {{'T', CD_edt1, "game.xy", 0}, {'K', VK_RETURN, 0, 0}, {'F', 0, 0, snap_mb},
                            {'B', IDOK, 0, 0},            {'F', 0, 0, snap_dlg_alive}, {'C', IDCANCEL, 0, 0}};
  Cfg c = site2("");
  begin();
  world();
  CHECK_EQ(vfs_setattr("C:\\STARS\\GAME.XY", VFS_ATTR_RO), 0);
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 1), 0);
  CHECK(strcmp(g_s[0], "box") == 0); /* OFN_NOREADONLYRETURN: refused with a single OK box */
  CHECK(strcmp(g_s[1], "alive") == 0);
  end();
}

static void test_buffer_too_small(void) {
  static const Step st[] = {{'T', CD_edt1, "test.m1", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  c.maxfile = 10;
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 0);
  CHECK_EQ(CommDlgExtendedError(), FNERR_BUFFERTOOSMALL);
  CHECK_EQ(*(uint16_t *)LIN(O()->lpstrFile), 17); /* C7 */
  end();
}

static void test_bad_struct(void) {
  Cfg c = site1(0);
  begin();
  world();
  mk_ofn(&c);
  O()->lStructSize = 0x40;
  CHECK_EQ(GetOpenFileName(g_ofn_fp), 0);
  CHECK_EQ(CommDlgExtendedError(), CDERR_STRUCTSIZE);
  CHECK_EQ(GetSaveFileName(g_ofn_fp), 0);
  CHECK_EQ(CommDlgExtendedError(), CDERR_STRUCTSIZE);
  CHECK_EQ(user_window_count(), 1);
  /* a good call resets the error */
  {
    static const Step st[] = {{'C', IDCANCEL, 0, 0}};
    O()->lStructSize = 0x48;
    CHECK_EQ(RUN(st, 0), 0);
    CHECK_EQ(CommDlgExtendedError(), 0);
  }
  end();
}

static void test_read_only_checkbox(void) {
  /* OFN_HIDEREADONLY off: the Read Only check box exists and sets OFN_READONLY when checked */
  static const Step st[] = {{'F', 0, 0, snap_open}, {'C', CD_chx1, 0, 0}, {'T', CD_edt1, "test.m1", 0}, {'K', VK_RETURN, 0, 0}};
  Cfg c = site1(0);
  begin();
  world();
  c.flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
  mk_ofn(&c);
  CHECK_EQ(RUN(st, 0), 1);
  CHECK(O()->Flags & OFN_READONLY);
  end();
}

static void test_printdlg_stub(void) {
  FARPTR p = far_alloc(PRINTDLG16_SIZE);
  uint8_t *b = LIN(p);
  memset(b, 0, PRINTDLG16_SIZE);
  b[0] = PRINTDLG16_SIZE;
  *(uint32_t *)(b + 0x0C) = 0x500;
  CHECK_EQ(PrintDlg(p), 0); /* as if the user cancelled */
  CHECK_EQ(CommDlgExtendedError(), 0);
}

/* The PASCAL stack adapters (harness/commdlg_thunks.c): COMMDLG 1, 2 and 20 are imports of the game. */
static uint32_t cd_thunk(uint16_t ord, const char *name, uint16_t argbytes, int ret) {
  uint32_t fp = harness_thunk_far("COMMDLG", ord);
  if (!fp) fp = harness_add_import("COMMDLG", ord, name, HK_PASCAL, argbytes, ret, NULL);
  CHECK(fp != 0);
  return fp;
}
static void test_thunks(void) {
  uint32_t open_t, save_t, print_t, err_t;
  Cfg c = site1(0);
  commdlg_register();
  open_t = cd_thunk(1, "GetOpenFileName", 4, HR_RET16);
  save_t = cd_thunk(2, "GetSaveFileName", 4, HR_RET16);
  print_t = cd_thunk(20, "PrintDlg", 4, HR_RET16);
  err_t = cd_thunk(26, "CommDlgExtendedError", 0, HR_RET32);
  begin();
  world();
  mk_ofn(&c);
  O()->lStructSize = 0x44; /* wrong size: no dialog, error 1, through the stack adapter */
  {
    uint16_t a[2] = {FP_SEG(g_ofn_fp), FP_OFF(g_ofn_fp)}; /* PASCAL: high word first */
    CHECK_EQ(harness_call_far(open_t, HCONV_PASCAL, a, 2) & 0xFFFF, 0);
    CHECK_EQ(harness_last_error(), 0);
    CHECK_EQ(harness_call_far(err_t, HCONV_PASCAL, NULL, 0), CDERR_STRUCTSIZE);
    CHECK_EQ(harness_call_far(save_t, HCONV_PASCAL, a, 2) & 0xFFFF, 0);
    CHECK_EQ(harness_call_far(err_t, HCONV_PASCAL, NULL, 0), CDERR_STRUCTSIZE);
    CHECK_EQ(harness_call_far(print_t, HCONV_PASCAL, a, 2) & 0xFFFF, 0);
    CHECK_EQ(harness_call_far(err_t, HCONV_PASCAL, NULL, 0), 0);
  }
  end();
}

int main(void) {
  (void)dump;
  if (load_fixture() != 0) return 1;
  test_game_strings();
  test_open_click();
  test_open_edit_follows_list();
  test_cancel();
  test_filter_combo();
  test_open_race_filter();
  test_typed_name();
  test_typed_with_keys();
  test_double_click_accepts();
  test_filemustexist();
  test_pathmustexist();
  test_directories();
  test_typed_path_changes_dir();
  test_wildcard_in_edit();
  test_save_game();
  test_save_race_site3();
  test_extension_cases();
  test_overwrite_prompt();
  test_overwrite_no_flag();
  test_overwrite_no_answer_cancels();
  test_readonly_save();
  test_buffer_too_small();
  test_bad_struct();
  test_read_only_checkbox();
  test_printdlg_stub();
  test_thunks();
  fprintf(stdout, "pixel tests: %d, all UNVERIFIED against Windows 3.1 (no tests/ref/NAME.png exists)\n", g_pixel_tests);
  fixture_close();
  free(g_img);
  return TEST_DONE();
}
