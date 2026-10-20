/* Dialog manager (task stars-0z4.12): see runtime/dialog.h for the model. Plain C, no harness
 * dependency. Every rule that no SDK text or reference capture backs is marked UNVERIFIED here
 * and in docs/inventory.md section 17; the behavioural reference is Wine
 * (wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0: dlls/user.exe16/dialog.c,
 * dlls/user32/dialog.c). The numbered notes U1..U10 are the ones listed in
 * runtime/tests/test_dialog.c. */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "runtime/dialog.h"
#include "runtime/kernel.h"
#include "runtime/ne.h"
#include "runtime/text.h"
#include "runtime/user_internal.h"

#define DLG_CLASS "#32770"

/* DlgX.flags */
#define DF_DIALOG 0x8000  /* the window is a dialog managed here (the extra bytes hold a DlgX) */
#define DF_MODAL 0x0001   /* created by DialogBox */
#define DF_END 0x0002     /* EndDialog was called */
#define DF_OWNER_OFF 0x0004 /* the owner was disabled by this dialog and still is */
#define DF_HASDEF 0x0008  /* defid is the default push button id (template or DM_SETDEFID) */
#define DF_INIT 0x0010    /* WM_INITDIALOG has been sent: activation may restore the focus */

/* The window extra bytes of a dialog: DWL_MSGRESULT, DWL_DLGPROC and DWL_USER first, as in Windows,
 * then the manager's state. Packed: the arena offset of the extra block has no alignment promise. */
typedef struct __attribute__((packed)) DlgX {
  uint32_t msgresult; /* DWL_MSGRESULT */
  uint32_t dlgproc;   /* DWL_DLGPROC */
  uint32_t user;      /* DWL_USER */
  uint16_t font;      /* HFONT created for DS_SETFONT, 0 otherwise (deleted with the window) */
  uint16_t focus;     /* the control that had the focus last (restored on activation) */
  uint16_t defid;     /* default push button id, valid with DF_HASDEF */
  uint16_t flags;
  int16_t result;     /* EndDialog's value */
  uint16_t owner;     /* the window disabled by a modal dialog */
  uint16_t xb, yb;    /* dialog base units */
} DlgX;
_Static_assert(sizeof(DlgX) <= DLGWINDOWEXTRA, "DlgX must fit the dialog window extra bytes");

static DlgX *dx(HWND16 h) {
  const UWnd *w = user_wnd(h);
  DlgX *x;
  if (!w || h == user_desktop() || !w->extra_off || w->nextra < DLGWINDOWEXTRA) return NULL;
  x = (DlgX *)(mem_arena() + w->extra_off);
  return (x->flags & DF_DIALOG) ? x : NULL;
}

/* ---- the dialog class ---- */

void user_dlg_init(void) {
  UserState *st = user_state();
  if (!st) return;
  for (unsigned i = 0; i < UC_MAX; i++) {
    UClass *c = &st->c[i];
    if (c->used) continue;
    memset(c, 0, sizeof *c);
    c->used = 1;
    c->wndproc = USER_CB_DIALOG;
    c->cbwnd = DLGWINDOWEXTRA;
    strcpy(c->name, DLG_CLASS);
    return;
  }
}

/* ---- window tree helpers ---- */

static bool is_desc(HWND16 dlg, HWND16 h) {
  for (const UWnd *w = user_wnd(h); w; w = user_wnd(w->parent)) {
    if (w->parent == dlg) return true;
    if (w->parent == user_desktop()) break;
  }
  return false;
}

static unsigned kids(HWND16 dlg, HWND16 out[UW_MAX]) {
  const UWnd *p = user_wnd(dlg);
  unsigned n = 0;
  if (!p) return 0;
  for (const UWnd *c = user_wnd(p->child); c && n < UW_MAX; c = user_wnd(c->next)) out[n++] = user_hwnd(c);
  return n;
}

static bool usable(HWND16 h) {
  const UWnd *w = user_wnd(h);
  return w && (w->style & WS_VISIBLE) && !(w->style & WS_DISABLED);
}

static uint16_t dlgcode(HWND16 h) { return (uint16_t)SendMessage(h, WM_GETDLGCODE, 0, 0); }

/* A BUTTON that is a (default) push button by style: those are the ones the default look moves over. */
static bool is_push(HWND16 h) {
  const UWnd *w = user_wnd(h);
  if (!w || (w->style & BS_TYPEMASK) > BS_DEFPUSHBUTTON) return false;
  return (dlgcode(h) & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON)) != 0;
}

/* GetNextDlgTabItem: the next (or previous) visible, enabled WS_TABSTOP direct child after cur in z
 * order (template order), wrapping; cur 0 starts at the first (last). Returns cur when it is the
 * only one, 0 when there is none. */
HWND16 user_dlg_next_tab(HWND16 dlg, HWND16 cur, bool prev) {
  HWND16 k[UW_MAX];
  unsigned n = kids(dlg, k);
  int idx = -1;
  for (unsigned i = 0; i < n; i++)
    if (k[i] == cur) idx = (int)i;
  for (int s = 1; s <= (int)n; s++) {
    int j = prev ? (idx < 0 ? (int)n : idx) - s : (idx < 0 ? -1 : idx) + s;
    const UWnd *w;
    j = ((j % (int)n) + (int)n) % (int)n;
    w = user_wnd(k[j]);
    if (w && (w->style & WS_TABSTOP) && usable(k[j])) return k[j];
  }
  return 0;
}

/* GetNextDlgGroupItem: inside the WS_GROUP group of cur (from the control with WS_GROUP up to the
 * next one), wrapping; labels (DLGC_STATIC) are skipped. Returns cur when nothing else qualifies. */
static HWND16 next_group(HWND16 dlg, HWND16 cur, bool prev) {
  HWND16 k[UW_MAX];
  unsigned n = kids(dlg, k);
  int idx = -1, gs, ge, len;
  for (unsigned i = 0; i < n; i++)
    if (k[i] == cur) idx = (int)i;
  if (idx < 0) return cur;
  gs = idx;
  while (gs > 0 && !(user_wnd(k[gs])->style & WS_GROUP)) gs--;
  ge = idx + 1;
  while (ge < (int)n && !(user_wnd(k[ge])->style & WS_GROUP)) ge++;
  len = ge - gs;
  for (int s = 1; s < len; s++) {
    int rel = ((idx - gs) + (prev ? -s : s)) % len, j;
    if (rel < 0) rel += len;
    j = gs + rel;
    if (usable(k[j]) && !(dlgcode(k[j]) & DLGC_STATIC)) return k[j];
  }
  return cur;
}

/* ---- default push button and focus (U4, U6) ---- */

/* The focus control decides which push button looks like the default: a focused push button, else
 * the default of the dialog (template BS_DEFPUSHBUTTON or DM_SETDEFID). BM_SETSTYLE only on a change. */
static void fix_default(HWND16 dlg, HWND16 nf) {
  DlgX *x = dx(dlg);
  HWND16 k[UW_MAX], want = 0;
  unsigned n;
  if (!x) return;
  if (nf && user_wnd(nf) && is_push(nf)) want = nf;
  else if (x->flags & DF_HASDEF) want = user_dlg_item(dlg, (int16_t)x->defid);
  n = kids(dlg, k);
  for (unsigned i = 0; i < n; i++) {
    unsigned cur, wantt;
    if (!user_wnd(k[i]) || !is_push(k[i])) continue;
    cur = user_wnd(k[i])->style & BS_TYPEMASK;
    wantt = k[i] == want ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON;
    if (cur != wantt) SendMessage(k[i], BM_SETSTYLE, (uint16_t)wantt, 1);
  }
}

/* DEFDLG_SetFocus: an edit control gets its text selected, the default look follows, then SetFocus. */
static void dlg_focus(HWND16 dlg, HWND16 c) {
  DlgX *x = dx(dlg);
  if (!x || !user_wnd(c)) return;
  if (dlgcode(c) & DLGC_HASSETSEL) SendMessage(c, EM_SETSEL, 0, make_long(0, 0xFFFF));
  fix_default(dlg, c);
  SetFocus(c);
  x = dx(dlg);
  if (x) x->focus = c;
}

static void save_focus(HWND16 dlg) {
  DlgX *x = dx(dlg);
  HWND16 f = GetFocus();
  if (x && f && f != dlg && is_desc(dlg, f)) x->focus = f;
}

/* Activation: the focus returns to the control that had it, the first WS_TABSTOP control when
 * there was none; a focus already inside the dialog is left alone (U4). */
static void restore_focus(HWND16 dlg) {
  DlgX *x = dx(dlg);
  HWND16 f = GetFocus(), t;
  const UWnd *w = user_wnd(dlg);
  if (!x || !(x->flags & DF_INIT) || (x->flags & DF_END) || !w || (w->style & WS_MINIMIZE)) return;
  if (f && f != dlg && is_desc(dlg, f)) {
    x->focus = f;
    return;
  }
  t = x->focus;
  if (!t || !user_wnd(t) || !is_desc(dlg, t) || !usable(t)) t = user_dlg_next_tab(dlg, 0, false);
  if (t) dlg_focus(dlg, t);
}

/* ---- keyboard: the dialog manager's part of IsDialogMessage ---- */

static void send_cmd(HWND16 dlg, uint16_t id, HWND16 ctl) { SendMessage(dlg, WM_COMMAND, id, make_long(ctl, BN_CLICKED)); }

/* A key press on a button: BM_CLICK did not exist in Windows 3.1, so the Space key is simulated
 * (pressed look, then the button's own click action and BN_CLICKED). */
static void press(HWND16 b) {
  SendMessage(b, WM_KEYDOWN, VK_SPACE, 0);
  if (user_wnd(b)) SendMessage(b, WM_KEYUP, VK_SPACE, 0);
}

static void do_arrow(HWND16 dlg, HWND16 f, bool prev) {
  HWND16 t;
  if (f == dlg || !user_wnd(f)) return;
  t = next_group(dlg, f, prev);
  if (t == f || !user_wnd(t)) return;
  dlg_focus(dlg, t);
  if (user_wnd(dlg) && user_wnd(t) && (dlgcode(t) & DLGC_RADIOBUTTON)) press(t); /* U7 */
}

static void do_enter(HWND16 dlg, HWND16 f, unsigned code) {
  uint32_t dw;
  /* U5: a focused push button is pressed ... */
  if (f != dlg && (code & (DLGC_DEFPUSHBUTTON | DLGC_UNDEFPUSHBUTTON))) {
    if (usable(f)) send_cmd(dlg, user_wnd(f)->id, f);
    return;
  }
  /* ... otherwise the default push button, with IDOK as the last resort */
  dw = SendMessage(dlg, DM_GETDEFID, 0, 0);
  if (!user_wnd(dlg)) return;
  if ((dw >> 16) == DC_HASDEFID) {
    HWND16 c = user_dlg_item(dlg, (int16_t)(dw & 0xFFFF));
    if (c && usable(c)) send_cmd(dlg, (uint16_t)(dw & 0xFFFF), c);
  } else {
    send_cmd(dlg, IDOK, user_dlg_item(dlg, IDOK));
  }
}

static bool has_mnemonic(const char *s, int ch) {
  for (; *s; s++) {
    if (*s != '&') continue;
    if (s[1] == '&') {
      s++;
      continue;
    }
    return tolower((unsigned char)s[1]) == ch;
  }
  return false;
}

/* U8: Alt+letter. The search starts after the focus control and wraps. */
static bool mnemonic(HWND16 dlg, unsigned ch) {
  HWND16 k[UW_MAX], f = GetFocus();
  unsigned n = kids(dlg, k);
  int start = 0;
  if (n == 0) return false;
  for (unsigned i = 0; i < n; i++)
    if (k[i] == f) start = (int)i + 1;
  for (unsigned s = 0; s < n; s++) {
    HWND16 c = k[((unsigned)start + s) % n];
    const UWnd *w = user_wnd(c);
    unsigned code;
    if (!w || !usable(c) || !has_mnemonic(w->text, (int)tolower((int)ch))) continue;
    code = dlgcode(c);
    if (code & DLGC_STATIC) { /* a label or group box: its next control */
      HWND16 t = HWND_TOP;
      for (unsigned i = 0; i < n; i++)
        if (k[i] == c) {
          for (unsigned j = 1; j <= n; j++) {
            HWND16 q = k[(i + j) % n];
            if (q != c && usable(q) && (user_wnd(q)->style & WS_TABSTOP)) {
              t = q;
              break;
            }
          }
          break;
        }
      if (t) dlg_focus(dlg, t);
    } else {
      dlg_focus(dlg, c);
      if (user_wnd(c) && (code & DLGC_BUTTON)) press(c);
    }
    return true;
  }
  return false;
}

/* Returns true when the key message was consumed by the dialog manager. */
static bool dlg_key(HWND16 dlg, MSG16 *m, FARPTR lpMsg) {
  HWND16 f = m->hwnd;
  unsigned code = 0;
  switch (m->message) {
    case WM_KEYDOWN:
      if (f != dlg) code = SendMessage(f, WM_GETDLGCODE, m->wParam, lpMsg) & 0xFFFF;
      if (code & DLGC_WANTMESSAGE) return false;
      switch (m->wParam) {
        case VK_TAB:
          if (code & DLGC_WANTTAB) return false;
          SendMessage(dlg, WM_NEXTDLGCTL, GetKeyState(VK_SHIFT) < 0 ? 1 : 0, 0);
          return true;
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
          if (code & DLGC_WANTARROWS) return false;
          do_arrow(dlg, f, m->wParam == VK_LEFT || m->wParam == VK_UP);
          return true;
        case VK_RETURN:
          do_enter(dlg, f, code);
          return true;
        case VK_ESCAPE:
          send_cmd(dlg, IDCANCEL, user_dlg_item(dlg, IDCANCEL));
          return true;
        default: return false;
      }
    case WM_SYSKEYDOWN:
      if (!(m->lParam & (1u << 29))) return false; /* Alt must be down (a lone Alt press is left to the system) */
      if ((m->wParam >= 'A' && m->wParam <= 'Z') || (m->wParam >= '0' && m->wParam <= '9')) return mnemonic(dlg, m->wParam);
      return false;
    case WM_SYSCHAR:
      return m->wParam < 128 && isalnum((int)m->wParam) && mnemonic(dlg, m->wParam);
    default: return false;
  }
}

uint16_t IsDialogMessage(HWND16 hDlg, FARPTR lpMsg) {
  MSG16 *m;
  if (!dx(hDlg) || !lpMsg || !mem_check(lpMsg, sizeof(MSG16))) return 0;
  m = LIN(lpMsg);
  if (m->message < WM_KEYFIRST || m->message > WM_KEYLAST) return 0;
  if (m->hwnd != hDlg && !is_desc(hDlg, m->hwnd)) return 0;
  if (!dlg_key(hDlg, m, lpMsg)) {
    TranslateMessage(lpMsg);
    DispatchMessage(lpMsg);
  }
  return 1;
}

/* ---- DefDlgProc ---- */

static uint32_t def_dlg(HWND16 h, DlgX *x, uint16_t msg, uint16_t wp, uint32_t lp) {
  switch (msg) {
    case WM_ERASEBKGND: { /* U10 */
      const UWnd *w = user_wnd(h);
      uint16_t brush = (uint16_t)SendMessage(h, WM_CTLCOLOR, wp, make_long(h, CTLCOLOR_DLG));
      if (w && brush) user_ctl_fill_brush(wp, r_make(0, 0, w->crect.right - w->crect.left, w->crect.bottom - w->crect.top), brush);
      return 1;
    }
    case WM_NCDESTROY: {
      uint16_t font = x->font;
      uint32_t r = user_def_window_proc(h, msg, wp, lp);
      if (font) DeleteObject(font);
      return r;
    }
    case WM_SHOWWINDOW:
      if (!wp) save_focus(h);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_ACTIVATE:
      if (wp == WA_INACTIVE) save_focus(h);
      else restore_focus(h);
      return user_def_window_proc(h, msg, wp, lp);
    case WM_SETFOCUS:
      restore_focus(h);
      return 0;
    case WM_NEXTDLGCTL: { /* wParam: the control (LOWORD(lParam) != 0) or 0 next / 1 previous */
      HWND16 dest;
      if (lp & 0xFFFF) dest = wp;
      else {
        HWND16 cur = GetFocus();
        if (!cur || !is_desc(h, cur)) cur = 0;
        dest = user_dlg_next_tab(h, cur, wp != 0);
      }
      if (dest && user_wnd(dest) && is_desc(h, dest)) dlg_focus(h, dest);
      return 0;
    }
    case DM_GETDEFID: {
      HWND16 k[UW_MAX];
      unsigned n;
      if ((x->flags & DF_HASDEF) && user_dlg_item(h, (int16_t)x->defid)) return make_long(x->defid, DC_HASDEFID);
      n = kids(h, k);
      for (unsigned i = 0; i < n; i++)
        if (user_wnd(k[i]) && (dlgcode(k[i]) & DLGC_DEFPUSHBUTTON)) return make_long(user_wnd(k[i])->id, DC_HASDEFID);
      return 0;
    }
    case DM_SETDEFID:
      x->defid = wp;
      x->flags |= DF_HASDEF;
      {
        HWND16 f = GetFocus();
        fix_default(h, f && is_desc(h, f) ? f : 0);
      }
      return 1;
    case WM_CLOSE: /* U9 */
      send_cmd(h, IDCANCEL, user_dlg_item(h, IDCANCEL));
      return 0;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}

uint32_t DefDlgProc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  DlgX *x = dx(h);
  if (!x) return user_def_window_proc(h, msg, wp, lp);
  if (x->dlgproc) {
    /* The dialog function returns a BOOL in AX only. */
    uint16_t r = (uint16_t)user_call_wndproc(x->dlgproc, h, msg, wp, lp);
    if (!user_wnd(h)) return r;
    x = dx(h);
    if (!x) return r;
    if (r) {
      switch (msg) {
        case WM_INITDIALOG:
        case WM_CTLCOLOR:
        case WM_COMPAREITEM:
        case WM_VKEYTOITEM:
        case WM_CHARTOITEM:
        case WM_QUERYDRAGICON: return r;
        default: return x->msgresult; /* DWL_MSGRESULT */
      }
    }
  }
  return def_dlg(h, x, msg, wp, lp);
}

uint32_t user_dialog_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) { return DefDlgProc(h, msg, wp, lp); }

/* ---- creation ---- */

static const char *const PREDEF[6] = {"BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX"};

static const char *class_name(const char *c) {
  if (c && c[0] == '#') {
    int v = atoi(c + 1);
    if (v >= 0x80 && v <= 0x85) return PREDEF[v - 0x80];
  }
  return c;
}

static FARPTR far_str(const char *s, uint16_t *handle) {
  FARPTR p = user_scratch((uint32_t)strlen(s) + 1, handle);
  if (p) strcpy(LIN(p), s);
  return p;
}

/* U1: the dialog font from the template and its base units (average character width and height). */
static void make_font_and_units(const NeDialog *d, uint16_t *font, int *xb, int *yb) {
  HDC16 dc;
  uint16_t hs = 0, hm = 0, old;
  FARPTR lf = user_scratch(LOGFONT16_SIZE, &hs), tm = user_scratch(TEXTMETRIC16_SIZE, &hm);
  *font = 0;
  *xb = 8;
  *yb = 16;
  if (!lf || !tm) goto out;
  if (d->has_font && d->font_name) {
    uint8_t *p = LIN(lf);
    int h = -MulDiv((int16_t)d->font_size, 96, 72);
    memset(p, 0, LOGFONT16_SIZE);
    p[LF_HEIGHT] = (uint8_t)(h & 255);
    p[LF_HEIGHT + 1] = (uint8_t)((h >> 8) & 255);
    p[LF_WEIGHT] = 400 & 255;
    p[LF_WEIGHT + 1] = 400 >> 8;
    p[LF_PITCHFAMILY] = 0x22; /* FF_SWISS | VARIABLE_PITCH */
    strncpy((char *)p + LF_FACENAME, d->font_name, LF_FACENAME_LEN - 1);
    *font = CreateFontIndirect(lf);
  }
  dc = GetDC(0);
  old = SelectObject(dc, *font ? *font : (uint16_t)GetStockObject(SYSTEM_FONT));
  if (GetTextMetrics(dc, tm)) {
    const uint8_t *q = LIN(tm);
    int h = q[0] | (q[1] << 8), w = q[10] | (q[11] << 8);
    if (w > 0) *xb = w;
    if (h > 0) *yb = h;
  }
  SelectObject(dc, old);
  ReleaseDC(0, dc);
out:
  user_scratch_free(hs);
  user_scratch_free(hm);
}

/* The top-level window a dialog is owned by (a WS_CHILD parent is walked up, as MessageBox does). */
static HWND16 owner_of(HWND16 parent) {
  HWND16 owner = (parent && user_wnd(parent) && parent != user_desktop()) ? parent : 0;
  while (owner) {
    const UWnd *ow = user_wnd(owner);
    if (!ow || !(ow->style & WS_CHILD) || !user_wnd(ow->parent) || ow->parent == user_desktop()) break;
    owner = ow->parent;
  }
  return owner;
}

/* Creates the dialog window and its controls hidden, sends WM_SETFONT and WM_INITDIALOG, and shows
 * it (U3). Returns 0 when anything cannot be created. */
static HWND16 build(const NeDialog *d, HANDLE16 hinst, HWND16 parent, FARPTR proc, uint32_t lparam, bool modal) {
  HWND16 owner = owner_of(parent), h = 0, first = 0;
  uint32_t style = d->style & ~(uint32_t)WS_VISIBLE, ex = (d->style & DS_MODALFRAME) ? WS_EX_DLGMODALFRAME : 0;
  int xb, yb, W, H, x, y, cw, ch;
  uint16_t font, hcls = 0, hcap = 0;
  FARPTR pcls, pcap;
  UNcLayout L;
  DlgX *dxp;
  const char *cname = (d->cls && d->cls[0] != '#') ? d->cls : DLG_CLASS;
  bool answer;
  make_font_and_units(d, &font, &xb, &yb);
  cw = MulDiv(d->cx, (int16_t)xb, 4);
  ch = MulDiv(d->cy, (int16_t)yb, 8);
  user_nc_layout_for(style, ex, 1000, 1000, &L);
  W = cw + L.client.left + (1000 - L.client.right);
  H = ch + L.client.top + (1000 - L.client.bottom);
  if (style & WS_CHILD) { /* U2: a child dialog sits at x, y in its parent's client area */
    owner = 0;
    x = MulDiv(d->x, (int16_t)xb, 4) - L.client.left;
    y = MulDiv(d->y, (int16_t)yb, 8) - L.client.top;
  } else {
    int ox = 0, oy = 0;
    if (owner && !(d->style & DS_ABSALIGN)) user_client_origin(user_wnd(owner), &ox, &oy);
    x = ox + MulDiv(d->x, (int16_t)xb, 4) - L.client.left;
    y = oy + MulDiv(d->y, (int16_t)yb, 8) - L.client.top;
    if (x + W > GDI_SCREEN_W) x = GDI_SCREEN_W - W;
    if (y + H > GDI_SCREEN_H) y = GDI_SCREEN_H - H;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
  }
  pcls = far_str(cname, &hcls);
  pcap = far_str(d->caption ? d->caption : "", &hcap);
  if (pcls && pcap)
    h = user_create_window_ex(ex, pcls, pcap, style, (int16_t)x, (int16_t)y, (int16_t)W, (int16_t)H,
                              (style & WS_CHILD) ? parent : owner, 0, hinst, 0);
  user_scratch_free(hcls);
  user_scratch_free(hcap);
  if (!h) {
    if (font) DeleteObject(font);
    return 0;
  }
  if (!user_wnd(h) || user_wnd(h)->nextra < DLGWINDOWEXTRA) { /* a custom class without the 30 extra bytes */
    DestroyWindow(h);
    if (font) DeleteObject(font);
    return 0;
  }
  dxp = (DlgX *)(mem_arena() + user_wnd(h)->extra_off);
  memset(dxp, 0, sizeof *dxp);
  dxp->flags = DF_DIALOG | (modal ? DF_MODAL : 0);
  dxp->dlgproc = proc;
  dxp->font = font;
  dxp->owner = owner;
  dxp->xb = (uint16_t)xb;
  dxp->yb = (uint16_t)yb;
  bool owner_off = false; /* the window (and its DlgX) may be gone by the time build() returns */
  if (modal && owner && IsWindowEnabled(owner)) {
    EnableWindow(owner, 0);
    dx(h)->flags |= DF_OWNER_OFF;
    owner_off = true;
  }
  if (font) SendMessage(h, WM_SETFONT, font, 0);
  for (size_t i = 0; i < d->n_items && user_wnd(h); i++) {
    const NeDialogItem *it = &d->items[i];
    const char *cn = class_name(it->cls);
    uint16_t h1 = 0, h2 = 0;
    FARPTR pc = far_str(cn, &h1), pt = far_str(it->text ? it->text : "", &h2);
    HWND16 c = 0;
    if (pc && pt)
      c = user_create_window_ex(0, pc, pt, it->style | WS_CHILD, (int16_t)MulDiv(it->x, (int16_t)xb, 4), (int16_t)MulDiv(it->y, (int16_t)yb, 8),
                                (int16_t)MulDiv(it->cx, (int16_t)xb, 4), (int16_t)MulDiv(it->cy, (int16_t)yb, 8), h, it->id, hinst, 0);
    user_scratch_free(h1);
    user_scratch_free(h2);
    if (!c) {
      if (user_wnd(h)) {
        DlgX *e = dx(h);
        if (e && (e->flags & DF_OWNER_OFF) && user_wnd(owner)) EnableWindow(owner, 1);
        DestroyWindow(h);
      }
      return 0;
    }
    if (font) SendMessage(c, WM_SETFONT, font, 0);
    if (user_wnd(h) && dx(h) && !(dx(h)->flags & DF_HASDEF) && cn && !strcasecmp(cn, "BUTTON") && (it->style & BS_TYPEMASK) == BS_DEFPUSHBUTTON) {
      dx(h)->defid = it->id; /* the template's default push button */
      dx(h)->flags |= DF_HASDEF;
    }
  }
  if (!user_wnd(h)) {
    if (owner_off && user_wnd(owner)) EnableWindow(owner, 1);
    return 0;
  }
  first = user_dlg_next_tab(h, 0, false);
  answer = (SendMessage(h, WM_INITDIALOG, first, lparam) & 0xFFFF) != 0; /* U4 */
  if (!user_wnd(h) || !dx(h)) {
    if (owner_off && user_wnd(owner)) EnableWindow(owner, 1);
    return 0;
  }
  dx(h)->flags |= DF_INIT;
  if (answer) dx(h)->focus = first;
  if (!(dx(h)->flags & DF_END) && ((d->style & WS_VISIBLE) || modal)) {
    ShowWindow(h, SW_SHOWNORMAL);
    if (user_wnd(h)) UpdateWindow(h); /* ShowWindow invalidated the controls too (stars-0z4.17) */
  }
  if (user_wnd(h) && dx(h) && !(dx(h)->flags & DF_END)) {
    if (answer && first && user_wnd(first)) dlg_focus(h, first);
    else {
      HWND16 f = GetFocus();
      fix_default(h, f && is_desc(h, f) ? f : 0);
    }
  }
  return user_wnd(h) ? h : 0;
}

static int load_template(HANDLE16 hinst, FARPTR name, NeDialog *d) {
  HANDLE16 hr = FindResource(hinst, name, MK_FP(0, RT_DIALOG)), hg;
  FARPTR p;
  uint32_t size;
  int rc = NE_ERR_FORMAT;
  if (!hr) return rc;
  size = SizeofResource(hinst, hr);
  hg = LoadResource(hinst, hr);
  if (!hg) return rc;
  p = LockResource(hg);
  if (p && size && mem_check(p, size)) rc = ne_parse_dialog(LIN(p), size, d);
  GlobalUnlock(hg);
  FreeResource(hg);
  return rc;
}

static HANDLE16 parent_hinst(HWND16 parent) {
  const UWnd *w = user_wnd(parent);
  return w ? w->hinst : 0;
}

HWND16 user_dialog_create(const uint8_t *tmpl, size_t len, HWND16 hWndParent, FARPTR lpDialogFunc, uint32_t lParam) {
  NeDialog d;
  HWND16 h;
  if (ne_parse_dialog(tmpl, len, &d) != NE_OK) return 0;
  h = build(&d, parent_hinst(hWndParent), hWndParent, lpDialogFunc, lParam, false);
  ne_dialog_free(&d);
  return h;
}

HWND16 CreateDialog(HANDLE16 hInstance, FARPTR lpTemplateName, HWND16 hWndParent, FARPTR lpDialogFunc) {
  NeDialog d;
  HWND16 h;
  if (load_template(hInstance, lpTemplateName, &d) != NE_OK) return 0;
  h = build(&d, hInstance, hWndParent, lpDialogFunc, 0, false);
  ne_dialog_free(&d);
  return h;
}

/* ---- the modal loop ---- */

static bool in_tree(HWND16 root, HWND16 h) {
  for (HWND16 w = h; w && user_wnd(w); w = user_wnd(w)->parent) {
    if (w == root) return true;
    if (w == user_desktop()) break;
  }
  return false;
}

static int16_t run_modal(HWND16 h, HWND16 owner, HWND16 prev_focus) {
  UPump pu;
  MSG16 m;
  DlgX *x = dx(h);
  int16_t result;
  if (x && !(x->flags & DF_END) && user_pump_open(&pu)) {
    while ((x = dx(h)) != NULL && !(x->flags & DF_END) && user_pump_get(&pu, &m)) {
      if (!IsDialogMessage(h, pu.p)) {
        TranslateMessage(pu.p);
        DispatchMessage(pu.p);
      }
    }
    user_pump_close(&pu);
  }
  x = dx(h);
  result = (x && (x->flags & DF_END)) ? x->result : 0;
  if (x && (x->flags & DF_OWNER_OFF) && user_wnd(owner)) {
    x->flags &= (uint16_t)~DF_OWNER_OFF;
    EnableWindow(owner, 1);
  }
  if (user_wnd(h)) DestroyWindow(h);
  if (owner && user_wnd(owner)) {
    SetActiveWindow(owner);
    /* the window that had the focus before the dialog gets it back when it is inside the owner */
    if (prev_focus && prev_focus != owner && user_wnd(prev_focus) && in_tree(owner, prev_focus) && IsWindowEnabled(prev_focus))
      SetFocus(prev_focus);
  }
  return result;
}

int16_t user_dialog_box(const uint8_t *tmpl, size_t len, HWND16 hWndParent, FARPTR lpDialogFunc, uint32_t lParam) {
  NeDialog d;
  HWND16 prev_focus = GetFocus(), h;
  if (ne_parse_dialog(tmpl, len, &d) != NE_OK) return -1;
  h = build(&d, parent_hinst(hWndParent), hWndParent, lpDialogFunc, lParam, true);
  ne_dialog_free(&d);
  if (!h) return -1;
  return run_modal(h, owner_of(hWndParent), prev_focus);
}

int16_t DialogBox(HANDLE16 hInstance, FARPTR lpTemplateName, HWND16 hWndParent, FARPTR lpDialogFunc) {
  NeDialog d;
  HWND16 prev_focus = GetFocus(), h;
  if (load_template(hInstance, lpTemplateName, &d) != NE_OK) return -1;
  h = build(&d, hInstance, hWndParent, lpDialogFunc, 0, true);
  ne_dialog_free(&d);
  if (!h) return -1;
  return run_modal(h, owner_of(hWndParent), prev_focus);
}

uint16_t EndDialog(HWND16 hDlg, int16_t nResult) {
  DlgX *x = dx(hDlg);
  if (!x || (x->flags & DF_END)) return 0;
  x->result = nResult;
  x->flags |= DF_END;
  if ((x->flags & DF_OWNER_OFF) && user_wnd(x->owner)) {
    x->flags &= (uint16_t)~DF_OWNER_OFF;
    EnableWindow(x->owner, 1);
  }
  ShowWindow(hDlg, SW_HIDE);
  PostMessage(hDlg, WM_NULL, 0, 0); /* wakes a modal loop that is waiting for a message */
  return 1;
}

bool user_dialog_units(HWND16 hDlg, int *xbase, int *ybase) {
  DlgX *x = dx(hDlg);
  if (!x) return false;
  *xbase = x->xb;
  *ybase = x->yb;
  return true;
}

/* ---- item helpers ---- */

HWND16 GetDlgItem(HWND16 hDlg, int16_t nIDDlgItem) { return user_dlg_item(hDlg, nIDDlgItem); }

void SetDlgItemText(HWND16 hDlg, int16_t nIDDlgItem, FARPTR lpString) {
  HWND16 c = user_dlg_item(hDlg, nIDDlgItem);
  if (c) SetWindowText(c, lpString);
}

int16_t GetDlgItemText(HWND16 hDlg, int16_t nIDDlgItem, FARPTR lpString, int16_t nMaxCount) {
  HWND16 c = user_dlg_item(hDlg, nIDDlgItem);
  if (!c) {
    if (nMaxCount > 0 && lpString && mem_check(lpString, 1)) *(char *)LIN(lpString) = 0;
    return 0;
  }
  return GetWindowText(c, lpString, nMaxCount);
}

uint32_t SendDlgItemMessage(HWND16 hDlg, int16_t nIDDlgItem, uint16_t msg, uint16_t wParam, uint32_t lParam) {
  HWND16 c = user_dlg_item(hDlg, nIDDlgItem);
  return c ? SendMessage(c, msg, wParam, lParam) : 0;
}
