/* COMMDLG (task stars-0z4.14): the File Open / Save As dialog over the virtual file system, and the
 * PrintDlg stub. See runtime/commdlg.h and docs/inventory.md section 19.
 *
 * Model. GetOpenFileName / GetSaveFileName allocate a FdState in the arena (rt_alloc), build a
 * Windows 3.x DLGTEMPLATE in host memory and run it with user_dialog_box; the dialog function is
 * user_filedlg_proc (callback segment offset 0x00A0, USER_CB_FILEDLG). The state offset travels as
 * the WM_INITDIALOG lParam and is kept in DWL_USER. The dialog's directory IS the VFS current
 * directory (vfs_chdir at every change); the original current directory is restored at the end when
 * OFN_NOCHANGEDIR is set.
 *
 * Every Windows 3.1 rule below that no SDK text or capture backs is marked UNVERIFIED here and in
 * docs/inventory.md section 19 (the SDK text is not on this machine and tests/ref/ is empty). */
#include "runtime/commdlg.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "runtime/ctl.h"
#include "runtime/dialog.h"
#include "runtime/user_internal.h"
#include "runtime/vfs.h"

/* ---- state (arena) ---- */

typedef struct FdState {
  uint32_t ofn;       /* FARPTR of the caller's OPENFILENAME */
  HWND16 dlg;         /* the dialog window while it is up */
  uint16_t save;      /* 1 = GetSaveFileName */
  uint16_t err;       /* error to publish when the dialog ends with FALSE */
  uint32_t need;      /* length needed when err = FNERR_BUFFERTOOSMALL */
  char orig_cwd[VFS_PATH_MAX];
  char pat[200];      /* the file pattern list in force ("*.M*;*.HST") */
  char title[80];
} FdState;

typedef struct CdRoot { /* COMMDLG globals, in the arena */
  uint32_t err;         /* CommDlgExtendedError */
  uint32_t active;      /* arena offset of the FdState being shown, 0 when none */
} CdRoot;

static uint32_t g_root_off; /* arena offset of the CdRoot (0 = not created); reset by commdlg_shutdown */

static CdRoot *root(void) {
  if (!g_root_off) {
    CdRoot *r = rt_alloc(sizeof *r);
    if (!r) return NULL;
    memset(r, 0, sizeof *r);
    g_root_off = mem_off(r);
  }
  return (CdRoot *)(mem_arena() + g_root_off);
}

void commdlg_shutdown(void) {
  if (g_root_off) rt_free(mem_arena() + g_root_off);
  g_root_off = 0;
}

static FdState *fd_at(uint32_t off) { return off ? (FdState *)(mem_arena() + off) : NULL; }
static FdState *fd_of(HWND16 h) { return fd_at(GetWindowLong(h, DWL_USER)); }
static OPENFILENAME16 *ofn_of(const FdState *s) { return (OPENFILENAME16 *)LIN(s->ofn); }

HWND16 commdlg_active_dialog(void) {
  CdRoot *r = g_root_off ? root() : NULL;
  FdState *s = r ? fd_at(r->active) : NULL;
  return s ? s->dlg : 0;
}

/* ---- small helpers ---- */

static char lower(char c) { return (char)tolower((unsigned char)c); }
static void lower_copy(char *dst, const char *src, size_t cap) {
  size_t i = 0;
  for (; src[i] && i + 1 < cap; i++) dst[i] = lower(src[i]);
  dst[i] = 0;
}

/* a far copy of s in global memory, freed with user_scratch_free(*h) */
static FARPTR scratch_str(const char *s, uint16_t *h) {
  FARPTR p = user_scratch((uint32_t)strlen(s) + 1, h);
  if (p) memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}
static void set_text(HWND16 d, int id, const char *s) {
  uint16_t h;
  FARPTR p = scratch_str(s, &h);
  if (!p) return;
  SetDlgItemText(d, (int16_t)id, p);
  user_scratch_free(h);
}
static uint32_t send_str(HWND16 d, int id, uint16_t msg, uint16_t wp, const char *s) {
  uint16_t h;
  FARPTR p = scratch_str(s, &h);
  uint32_t r;
  if (!p) return 0;
  r = SendDlgItemMessage(d, (int16_t)id, msg, wp, p);
  user_scratch_free(h);
  return r;
}
static void get_text(HWND16 d, int id, char *out, size_t cap) {
  uint16_t h;
  FARPTR p = user_scratch((uint32_t)cap, &h);
  out[0] = 0;
  if (!p) return;
  GetDlgItemText(d, (int16_t)id, p, (int16_t)cap);
  snprintf(out, cap, "%s", (const char *)LIN(p));
  user_scratch_free(h);
}
static void msgbox(HWND16 d, const FdState *s, const char *text, uint16_t type, int16_t *answer) {
  uint16_t h1, h2;
  FARPTR t = scratch_str(text, &h1), c = scratch_str(s->title, &h2);
  int16_t r = MessageBox(d, t, c, type);
  if (answer) *answer = r;
  user_scratch_free(h1);
  user_scratch_free(h2);
}

/* ---- DOS wild cards (UNVERIFIED for 3.1: *, ? on the 8.3 name and extension parts) ---- */

static bool glob(const char *p, const char *s) {
  while (*p) {
    if (*p == '*') {
      while (*p == '*') p++;
      if (!*p) return true;
      for (; *s; s++)
        if (glob(p, s)) return true;
      return false;
    }
    if (*s && (*p == '?' || toupper((unsigned char)*p) == toupper((unsigned char)*s))) {
      p++;
      s++;
    } else {
      return false;
    }
  }
  return *s == 0;
}
static bool dos_match1(const char *pat, const char *name) {
  char pn[32], pe[32], nn[32], ne[32];
  const char *d;
  d = strchr(pat, '.');
  if (d) {
    snprintf(pn, sizeof pn, "%.*s", (int)(d - pat), pat);
    snprintf(pe, sizeof pe, "%s", d + 1);
  } else {
    snprintf(pn, sizeof pn, "%s", pat);
    snprintf(pe, sizeof pe, "%s", strcmp(pat, "*") == 0 ? "*" : "");
  }
  d = strchr(name, '.');
  if (d) {
    snprintf(nn, sizeof nn, "%.*s", (int)(d - name), name);
    snprintf(ne, sizeof ne, "%s", d + 1);
  } else {
    snprintf(nn, sizeof nn, "%s", name);
    ne[0] = 0;
  }
  return glob(pn, nn) && glob(pe, ne);
}
/* pat is a list of patterns separated by ';' */
static bool dos_match(const char *list, const char *name) {
  char tok[64];
  const char *p = list;
  while (*p) {
    size_t n = 0;
    while (*p == ' ') p++;
    while (p[n] && p[n] != ';') n++;
    snprintf(tok, sizeof tok, "%.*s", (int)(n < sizeof tok ? n : sizeof tok - 1), p);
    while (strlen(tok) && tok[strlen(tok) - 1] == ' ') tok[strlen(tok) - 1] = 0;
    if (tok[0] && dos_match1(tok, name)) return true;
    p += n;
    if (*p == ';') p++;
  }
  return false;
}
static bool has_wild(const char *s) { return strpbrk(s, "*?") != NULL; }

/* ---- the filter string (pairs "text\0pattern\0", ended by an extra NUL) ---- */

#define MAX_FILTERS 16
typedef struct Filters {
  int n;
  char desc[MAX_FILTERS][80];
  char pat[MAX_FILTERS][200];
} Filters;

static void parse_filters(FARPTR fp, Filters *f) {
  const char *p;
  f->n = 0;
  if (!fp || !mem_check(fp, 1)) return;
  p = LIN(fp);
  for (int guard = 0; *p && f->n < MAX_FILTERS && guard < 2048; guard++) {
    snprintf(f->desc[f->n], sizeof f->desc[0], "%s", p);
    p += strlen(p) + 1;
    snprintf(f->pat[f->n], sizeof f->pat[0], "%s", p);
    p += strlen(p) + 1;
    f->n++;
  }
}

/* ---- lists ---- */

#define MAX_ENTS 256
typedef struct Ents {
  int n;
  char name[MAX_ENTS][13];
} Ents;
typedef struct ListCtx {
  Ents *files, *dirs;
  const char *pat;
} ListCtx;
static void list_cb(const char *name, int is_dir, uint32_t size, void *ud) {
  ListCtx *c = ud;
  Ents *e = is_dir ? c->dirs : c->files;
  (void)size;
  if (!is_dir && !dos_match(c->pat, name)) return;
  if (e->n < MAX_ENTS) snprintf(e->name[e->n++], sizeof e->name[0], "%s", name);
}

static void refresh(HWND16 d, FdState *s) {
  Ents files, dirs; /* scratch only, filled and consumed within this call */
  ListCtx c;
  char cwd[VFS_PATH_MAX], low[VFS_PATH_MAX], comp[VFS_PATH_MAX];
  const char *p;
  int depth = 1;
  files.n = dirs.n = 0;
  c.files = &files;
  c.dirs = &dirs;
  c.pat = s->pat;
  vfs_getcwd(cwd);
  vfs_list(cwd, list_cb, &c);
  /* files: sorted by the list box (LBS_SORT) */
  SendDlgItemMessage(d, CD_lst1, LB_RESETCONTENT, 0, 0);
  for (int i = 0; i < files.n; i++) {
    lower_copy(low, files.name[i], sizeof low);
    send_str(d, CD_lst1, LB_ADDSTRING, 0, low);
  }
  /* directories: C3 "c:\", each directory down to the current one, then the sub-directories */
  SendDlgItemMessage(d, CD_lst2, LB_RESETCONTENT, 0, 0);
  {
    uint32_t r = send_str(d, CD_lst2, LB_ADDSTRING, 0, "c:\\");
    SendDlgItemMessage(d, CD_lst2, LB_SETITEMDATA, (uint16_t)r, 1);
  }
  for (p = cwd + 3; *p;) {
    size_t n = strcspn(p, "\\");
    snprintf(comp, sizeof comp, "%.*s", (int)n, p);
    lower_copy(low, comp, sizeof low);
    depth++;
    {
      uint32_t r = send_str(d, CD_lst2, LB_ADDSTRING, 0, low);
      SendDlgItemMessage(d, CD_lst2, LB_SETITEMDATA, (uint16_t)r, depth);
    }
    p += n;
    if (*p == '\\') p++;
  }
  SendDlgItemMessage(d, CD_lst2, LB_SETCURSEL, (uint16_t)(depth - 1), 0);
  for (int i = 1; i < dirs.n; i++) /* insertion sort */
    for (int j = i; j > 0 && strcmp(dirs.name[j - 1], dirs.name[j]) > 0; j--) {
      char t[13];
      memcpy(t, dirs.name[j], 13);
      memcpy(dirs.name[j], dirs.name[j - 1], 13);
      memcpy(dirs.name[j - 1], t, 13);
    }
  for (int i = 0; i < dirs.n; i++) {
    uint32_t r;
    lower_copy(low, dirs.name[i], sizeof low);
    r = send_str(d, CD_lst2, LB_ADDSTRING, 0, low);
    SendDlgItemMessage(d, CD_lst2, LB_SETITEMDATA, (uint16_t)r, 0x100);
  }
  lower_copy(low, cwd, sizeof low);
  set_text(d, CD_stc1, low);
}

static bool is_dir_path(const char *path, char *full) {
  uint8_t at;
  char tmp[VFS_PATH_MAX];
  if (vfs_resolve(path, tmp, 0) != 0) return false;
  if (vfs_getattr(tmp, &at) != 0 || !(at & VFS_ATTR_DIR)) return false;
  if (full) strcpy(full, tmp);
  return true;
}
static bool change_dir(HWND16 d, FdState *s, const char *path) {
  char full[VFS_PATH_MAX];
  if (!is_dir_path(path, full)) return false;
  if (vfs_chdir(full) != 0) return false;
  refresh(d, s);
  return true;
}

/* ---- the template ---- */

typedef struct Tb {
  uint8_t b[2048];
  size_t n;
} Tb;
static void t8(Tb *t, unsigned v) { t->b[t->n++] = (uint8_t)v; }
static void t16(Tb *t, unsigned v) { t8(t, v & 255); t8(t, v >> 8); }
static void t32(Tb *t, uint32_t v) { t16(t, v & 0xFFFF); t16(t, v >> 16); }
static void tstr(Tb *t, const char *s) {
  size_t l = strlen(s) + 1;
  memcpy(t->b + t->n, s, l);
  t->n += l;
}
static void titem(Tb *t, int cls, int x, int y, int cx, int cy, unsigned id, uint32_t style, const char *text) {
  t16(t, (unsigned)x);
  t16(t, (unsigned)y);
  t16(t, (unsigned)cx);
  t16(t, (unsigned)cy);
  t16(t, id);
  t32(t, style);
  t8(t, (unsigned)cls);
  tstr(t, text);
  t8(t, 0);
}

#define CLS_BUTTON 0x80
#define CLS_EDIT 0x81
#define CLS_STATIC 0x82
#define CLS_LISTBOX 0x83
#define CLS_COMBOBOX 0x85
#define W_CHILD_VIS (WS_CHILD | WS_VISIBLE)

/* The File Open / Save As layout, in dialog units (UNVERIFIED: no reference capture; the
 * arrangement is the one recalled from Windows 3.1: file name column, directories column, buttons
 * at the right; 36,24 is the position of the SDK's FILEOPEN template as recalled). */
static void build_template(Tb *t, const char *title, uint32_t ofn_flags) {
  bool ro = !(ofn_flags & OFN_HIDEREADONLY);
  unsigned n = 12 + (ro ? 1 : 0);
  t->n = 0;
  t32(t, WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | WS_VISIBLE);
  t8(t, n);
  t16(t, 36);
  t16(t, 24);
  t16(t, 280);
  t16(t, 134);
  t8(t, 0); /* no menu */
  t8(t, 0); /* default class */
  tstr(t, title);
  t16(t, 8);
  tstr(t, "MS Sans Serif");
  titem(t, CLS_STATIC, 6, 6, 90, 9, 0xFFFF, W_CHILD_VIS, "File &Name:");
  titem(t, CLS_EDIT, 6, 16, 90, 12, CD_edt1, W_CHILD_VIS | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, "");
  titem(t, CLS_LISTBOX, 6, 32, 90, 68, CD_lst1,
        W_CHILD_VIS | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_SORT | LBS_HASSTRINGS, "");
  titem(t, CLS_STATIC, 6, 104, 90, 9, 0xFFFF, W_CHILD_VIS, "List Files of &Type:");
  titem(t, CLS_COMBOBOX, 6, 114, 90, 36, CD_cmb1,
        W_CHILD_VIS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, "");
  titem(t, CLS_STATIC, 110, 6, 92, 9, 0xFFFF, W_CHILD_VIS, "&Directories:");
  titem(t, CLS_STATIC, 110, 18, 92, 9, CD_stc1, W_CHILD_VIS | SS_NOPREFIX, "");
  titem(t, CLS_LISTBOX, 110, 32, 92, 68, CD_lst2,
        W_CHILD_VIS | WS_BORDER | WS_VSCROLL | WS_TABSTOP | LBS_NOTIFY | LBS_HASSTRINGS, "");
  titem(t, CLS_STATIC, 110, 104, 92, 9, 0xFFFF, W_CHILD_VIS, "Dri&ves:");
  titem(t, CLS_COMBOBOX, 110, 114, 92, 68, CD_cmb2,
        W_CHILD_VIS | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST | CBS_HASSTRINGS, "");
  titem(t, CLS_BUTTON, 208, 6, 60, 14, IDOK, W_CHILD_VIS | WS_GROUP | WS_TABSTOP | BS_DEFPUSHBUTTON, "OK");
  titem(t, CLS_BUTTON, 208, 24, 60, 14, IDCANCEL, W_CHILD_VIS | WS_TABSTOP | BS_PUSHBUTTON, "Cancel");
  if (ro) titem(t, CLS_BUTTON, 208, 44, 60, 12, CD_chx1, W_CHILD_VIS | WS_TABSTOP | BS_AUTOCHECKBOX, "&Read Only");
}

/* ---- accepting a file name ---- */

static const char *ext_of(const char *name) {
  const char *dot = strrchr(name, '.');
  return dot ? dot + 1 : NULL;
}

static void apply_pattern(HWND16 d, FdState *s, const char *txt) {
  char dirpart[VFS_PATH_MAX], pat[80];
  const char *sep = strrchr(txt, '\\');
  if (!sep && txt[1] == ':') sep = txt + 1;
  if (sep) {
    snprintf(dirpart, sizeof dirpart, "%.*s", (int)(sep - txt) + (sep == txt || sep[0] == ':' ? 1 : 0), txt);
    snprintf(pat, sizeof pat, "%s", sep + 1);
    if (dirpart[0] && !change_dir(d, s, dirpart)) {
      char m[200];
      snprintf(m, sizeof m, "%s\nPath does not exist.", txt); /* C6 UNVERIFIED text */
      msgbox(d, s, m, MB_OK | MB_ICONEXCLAMATION, NULL);
      return;
    }
  } else {
    snprintf(pat, sizeof pat, "%s", txt);
  }
  if (pat[0]) snprintf(s->pat, sizeof s->pat, "%s", pat);
  refresh(d, s);
  set_text(d, CD_edt1, s->pat);
}

static void accept(HWND16 d, FdState *s) {
  OPENFILENAME16 *o = ofn_of(s);
  char txt[260], tmp[300], full[VFS_PATH_MAX], dirpart[VFS_PATH_MAX], msg[360];
  const char *name, *defext = "", *slash;
  uint32_t flags = o->Flags;
  uint8_t at = 0;
  bool exists, ended_dot;
  size_t n, len;
  get_text(d, CD_edt1, txt, sizeof txt);
  n = strlen(txt);
  while (n && txt[n - 1] == ' ') txt[--n] = 0;
  {
    char *q = txt;
    while (*q == ' ') q++;
    if (q != txt) memmove(txt, q, strlen(q) + 1);
  }
  n = strlen(txt);
  if (!txt[0]) return;
  if (has_wild(txt)) { /* C2 */
    apply_pattern(d, s, txt);
    return;
  }
  if (is_dir_path(txt, NULL)) { /* a directory typed in the edit changes the directory */
    change_dir(d, s, txt);
    set_text(d, CD_edt1, s->pat);
    return;
  }
  ended_dot = txt[n - 1] == '.';
  snprintf(tmp, sizeof tmp, "%s", txt);
  if (o->lpstrDefExt && mem_check(o->lpstrDefExt, 1)) defext = LIN(o->lpstrDefExt);
  if (!strrchr(strrchr(txt, '\\') ? strrchr(txt, '\\') : txt, '.') && defext[0]) /* C5 */
    snprintf(tmp, sizeof tmp, "%s.%.3s", txt, defext);
  if (ended_dot) tmp[strlen(tmp) - 1] = 0; /* the VFS refuses a trailing '.': "MINE." names MINE (UNVERIFIED, C5) */
  if (vfs_resolve(tmp, full, 0) != 0) {
    snprintf(msg, sizeof msg, "%s\nInvalid file name.", txt); /* C6 */
    msgbox(d, s, msg, MB_OK | MB_ICONEXCLAMATION, NULL);
    return;
  }
  slash = strrchr(full, '\\');
  name = slash + 1;
  snprintf(dirpart, sizeof dirpart, "%.*s", slash - full == 2 ? 3 : (int)(slash - full), full);
  if ((flags & (OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST)) && !is_dir_path(dirpart, NULL)) {
    snprintf(msg, sizeof msg, "%s\nPath does not exist.", full); /* C6 */
    msgbox(d, s, msg, MB_OK | MB_ICONEXCLAMATION, NULL);
    return;
  }
  exists = vfs_getattr(full, &at) == 0;
  if ((flags & OFN_FILEMUSTEXIST) && !exists) {
    snprintf(msg, sizeof msg, "%s\nFile not found.", full); /* C6 */
    msgbox(d, s, msg, MB_OK | MB_ICONEXCLAMATION, NULL);
    return;
  }
  if (s->save && (flags & OFN_NOREADONLYRETURN) && exists && (at & VFS_ATTR_RO)) {
    snprintf(msg, sizeof msg, "%s\nThis file is read-only.", full); /* C6 */
    msgbox(d, s, msg, MB_OK | MB_ICONEXCLAMATION, NULL);
    return;
  }
  if (s->save && (flags & OFN_OVERWRITEPROMPT) && exists) {
    int16_t ans = 0;
    snprintf(msg, sizeof msg, "%s already exists.\nDo you want to replace it?", full); /* C6 */
    msgbox(d, s, msg, MB_YESNO | MB_ICONEXCLAMATION, &ans);
    if (ans != IDYES) return;
  }
  len = strlen(full);
  o = ofn_of(s);
  if (!o->lpstrFile || len + 1 > o->nMaxFile || !mem_check(o->lpstrFile, (uint32_t)len + 1)) {
    s->err = FNERR_BUFFERTOOSMALL; /* C7 */
    s->need = (uint32_t)len + 1;
    if (o->lpstrFile && o->nMaxFile >= 2 && mem_check(o->lpstrFile, 2)) *(uint16_t *)LIN(o->lpstrFile) = (uint16_t)(len + 1);
    EndDialog(d, 0);
    return;
  }
  memcpy(LIN(o->lpstrFile), full, len + 1);
  o->nFileOffset = (uint16_t)(name - full);
  {
    const char *e = ext_of(name);
    o->nFileExtension = ended_dot ? 0 : (e ? (uint16_t)(e - full) : (uint16_t)len); /* C5 */
    o->Flags &= ~(OFN_EXTENSIONDIFFERENT | OFN_READONLY);
    if (defext[0] && e && e[0] && strncasecmp(e, defext, 3) != 0) o->Flags |= OFN_EXTENSIONDIFFERENT;
  }
  if (GetDlgItem(d, CD_chx1) && IsDlgButtonChecked(d, CD_chx1)) o->Flags |= OFN_READONLY;
  if (o->lpstrFileTitle && o->nMaxFileTitle > 0 && mem_check(o->lpstrFileTitle, 1)) {
    uint32_t cap = o->nMaxFileTitle;
    snprintf((char *)LIN(o->lpstrFileTitle), cap, "%s", name);
  }
  {
    int sel = (int)(int16_t)SendDlgItemMessage(d, CD_cmb1, CB_GETCURSEL, 0, 0);
    o->nFilterIndex = sel >= 0 ? (uint32_t)sel + 1 : 1;
  }
  /* a typed path moves the current directory to its directory part: the game opens the BARE name
   * (lpstrFile + nFileOffset) relative to it (site 1). OFN_NOCHANGEDIR is restored by file_dialog. */
  vfs_chdir(dirpart);
  EndDialog(d, 1);
}

/* ---- the dialog function ---- */

static void select_filter(HWND16 d, FdState *s, int idx) {
  Filters f;
  parse_filters(ofn_of(s)->lpstrFilter, &f);
  if (idx < 0 || idx >= f.n) return;
  snprintf(s->pat, sizeof s->pat, "%s", f.pat[idx]);
  refresh(d, s);
  set_text(d, CD_edt1, s->pat); /* C2 */
}

static void init_dialog(HWND16 d, FdState *s) {
  OPENFILENAME16 *o = ofn_of(s);
  Filters f;
  char cwd[VFS_PATH_MAX], file[260] = "";
  s->dlg = d;
  /* title */
  if (o->lpstrTitle && mem_check(o->lpstrTitle, 1) && ((const char *)LIN(o->lpstrTitle))[0])
    snprintf(s->title, sizeof s->title, "%s", (const char *)LIN(o->lpstrTitle));
  /* filters */
  parse_filters(o->lpstrFilter, &f);
  for (int i = 0; i < f.n; i++) send_str(d, CD_cmb1, CB_ADDSTRING, 0, f.desc[i]);
  {
    int idx = o->nFilterIndex >= 1 && o->nFilterIndex <= (uint32_t)f.n ? (int)o->nFilterIndex - 1 : 0;
    if (f.n) {
      SendDlgItemMessage(d, CD_cmb1, CB_SETCURSEL, (uint16_t)idx, 0);
      snprintf(s->pat, sizeof s->pat, "%s", f.pat[idx]);
    } else {
      snprintf(s->pat, sizeof s->pat, "*.*");
    }
  }
  send_str(d, CD_cmb2, CB_ADDSTRING, 0, "c:");
  SendDlgItemMessage(d, CD_cmb2, CB_SETCURSEL, 0, 0);
  /* directory: lpstrInitialDir when it names a directory, else the current directory */
  if (o->lpstrInitialDir && mem_check(o->lpstrInitialDir, 1)) {
    const char *id = LIN(o->lpstrInitialDir);
    if (id[0] && is_dir_path(id, cwd)) vfs_chdir(cwd);
  }
  refresh(d, s);
  /* File Name edit: lpstrFile, else the pattern (C1) */
  if (o->lpstrFile && mem_check(o->lpstrFile, 1)) snprintf(file, sizeof file, "%s", (const char *)LIN(o->lpstrFile));
  set_text(d, CD_edt1, file[0] ? file : s->pat);
  SendDlgItemMessage(d, CD_edt1, EM_LIMITTEXT, 259, 0);
  {
    uint16_t h;
    FARPTR p = scratch_str(s->title, &h);
    if (p) {
      SetWindowText(d, p);
      user_scratch_free(h);
    }
  }
}

uint32_t user_filedlg_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  FdState *s;
  if (msg == WM_INITDIALOG) {
    s = fd_at(lp);
    if (!s) return 0;
    SetWindowLong(h, DWL_USER, lp);
    init_dialog(h, s);
    return 1;
  }
  s = fd_of(h);
  if (!s) return 0;
  if (msg == WM_COMMAND) {
    unsigned code = (unsigned)(lp >> 16);
    switch (wp) {
      case IDOK: accept(h, s); return 1;
      case IDCANCEL: EndDialog(h, 0); return 1;
      case CD_lst1:
        if (code == LBN_SELCHANGE || code == LBN_DBLCLK) {
          char nm[40];
          int sel = (int)(int16_t)SendDlgItemMessage(h, CD_lst1, LB_GETCURSEL, 0, 0);
          uint16_t hs;
          FARPTR p = user_scratch(40, &hs);
          if (sel < 0 || !p) return 1;
          SendDlgItemMessage(h, CD_lst1, LB_GETTEXT, (uint16_t)sel, p);
          snprintf(nm, sizeof nm, "%s", (const char *)LIN(p));
          user_scratch_free(hs);
          set_text(h, CD_edt1, nm); /* C2 */
          if (code == LBN_DBLCLK) accept(h, s);
        }
        return 1;
      case CD_lst2:
        if (code == LBN_DBLCLK) { /* C3 */
          int sel = (int)(int16_t)SendDlgItemMessage(h, CD_lst2, LB_GETCURSEL, 0, 0);
          uint32_t data;
          char nm[40], cwd[VFS_PATH_MAX], target[VFS_PATH_MAX];
          uint16_t hs;
          FARPTR p = user_scratch(40, &hs);
          if (sel < 0 || !p) return 1;
          SendDlgItemMessage(h, CD_lst2, LB_GETTEXT, (uint16_t)sel, p);
          snprintf(nm, sizeof nm, "%s", (const char *)LIN(p));
          user_scratch_free(hs);
          data = SendDlgItemMessage(h, CD_lst2, LB_GETITEMDATA, (uint16_t)sel, 0);
          vfs_getcwd(cwd);
          if (data == 0x100) { /* a sub-directory */
            snprintf(target, sizeof target, "%s%s%s", cwd, cwd[3] ? "\\" : "", nm);
          } else { /* an ancestor: depth 1 = "c:\", the first (depth - 1) components */
            int keep = (int)data - 1;
            const char *q = cwd + 3;
            snprintf(target, sizeof target, "C:\\");
            while (keep > 0 && *q) {
              size_t n = strcspn(q, "\\");
              size_t tl = strlen(target);
              snprintf(target + tl, sizeof target - tl, "%s%.*s", tl > 3 ? "\\" : "", (int)n, q);
              q += n;
              if (*q == '\\') q++;
              keep--;
            }
          }
          if (change_dir(h, s, target)) set_text(h, CD_edt1, s->pat);
        }
        return 1;
      case CD_cmb1:
        if (code == CBN_SELCHANGE) select_filter(h, s, (int)(int16_t)SendDlgItemMessage(h, CD_cmb1, CB_GETCURSEL, 0, 0));
        return 1;
      default: break;
    }
  }
  return 0;
}

/* ---- the Win16 API ---- */

static uint16_t file_dialog(FARPTR fp, int save) {
  CdRoot *r = root();
  OPENFILENAME16 *o;
  FdState *s;
  uint32_t prev_active;
  int16_t res;
  Tb *t;
  const char *deftitle = save ? "Save As" : "Open";
  if (!r) return 0;
  r->err = 0;
  if (!fp || !mem_check(fp, OPENFILENAME16_SIZE)) {
    r->err = CDERR_INITIALIZATION;
    return 0;
  }
  o = LIN(fp);
  if (o->lStructSize != OPENFILENAME16_SIZE) {
    r->err = CDERR_STRUCTSIZE;
    return 0;
  }
  if (!o->lpstrFile || o->nMaxFile == 0) {
    r->err = CDERR_INITIALIZATION; /* UNVERIFIED code */
    return 0;
  }
  s = rt_alloc(sizeof *s);
  t = rt_alloc(sizeof *t);
  if (!s || !t) {
    if (s) rt_free(s);
    if (t) rt_free(t);
    r->err = CDERR_INITIALIZATION;
    return 0;
  }
  memset(s, 0, sizeof *s);
  s->ofn = fp;
  s->save = (uint16_t)save;
  snprintf(s->title, sizeof s->title, "%s", deftitle);
  if (o->lpstrTitle && mem_check(o->lpstrTitle, 1) && ((const char *)LIN(o->lpstrTitle))[0])
    snprintf(s->title, sizeof s->title, "%s", (const char *)LIN(o->lpstrTitle));
  vfs_getcwd(s->orig_cwd);
  build_template(t, s->title, o->Flags);
  prev_active = r->active;
  r->active = mem_off(s);
  res = user_dialog_box(t->b, t->n, o->hwndOwner, USER_CB_FILEDLG, mem_off(s));
  r = root(); /* (the arena does not move; kept for clarity) */
  r->active = prev_active;
  if (res == -1) s->err = CDERR_DIALOGFAILURE;
  if (ofn_of(s)->Flags & OFN_NOCHANGEDIR) vfs_chdir(s->orig_cwd);
  r->err = (res == 1) ? 0 : s->err;
  rt_free(t);
  rt_free(s);
  return res == 1;
}

uint16_t GetOpenFileName(FARPTR lpofn) { return file_dialog(lpofn, 0); }
uint16_t GetSaveFileName(FARPTR lpofn) { return file_dialog(lpofn, 1); }

uint16_t PrintDlg(FARPTR lppd) {
  CdRoot *r = root();
  (void)lppd;
  if (r) r->err = 0; /* printing is stars-0z4.16: answer as if the user cancelled */
  return 0;
}

uint32_t CommDlgExtendedError(void) {
  CdRoot *r = root();
  return r ? r->err : 0;
}
