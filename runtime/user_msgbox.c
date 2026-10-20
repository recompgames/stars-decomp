/* Windows 3.1 look, MessageBox (task stars-0z4.9).
 *
 * A modal box in the Windows 3.1 layout, drawn in code (no dialog template: the dialog manager is
 * stars-0z4.12): a dialog frame (WS_EX_DLGMODALFRAME) round a caption, a white (COLOR_WINDOW)
 * client area, the icon (MB_ICON*) at the left, the word-wrapped text to its right, and a row of
 * buttons (MB_OK ... MB_RETRYCANCEL) centred below. The default button (MB_DEFBUTTON1..3) has a
 * two pixel outline; Enter presses it, Space presses the focused one, Tab and the arrow keys move
 * the focus (the focused button becomes the default one), Escape answers Cancel (or OK for a lone
 * OK button; nothing otherwise), the underlined letter of Yes / No / Abort / Retry / Ignore
 * answers directly. The box runs its own message loop; the owner is disabled while it is up.
 *
 * All sizes below are this runtime's reading of the Windows 3.1 look and UNVERIFIED against
 * Windows 3.1 (docs/inventory.md section 13, "Windows 3.1 look"): margins 16, icon 32x32 with
 * 16 pixels to the text, text wrapped at 288 pixels, buttons 26 high, at least 64 wide, 12 apart,
 * the box centred on the screen. Plain C, no harness dependency. */
#include <string.h>

#include "runtime/text.h"
#include "runtime/user_internal.h"

#define COL(i) user_syscolor(i)
#define ML 16
#define MT 16
#define BTN_H 26
#define BTN_GAP 12
#define BTN_MINW 64
#define TEXT_MAX 288
#define MB_CLASS "MsgBoxWin31"
#define VK_LEFT_ 0x25
#define VK_UP_ 0x26
#define VK_RIGHT_ 0x27
#define VK_DOWN_ 0x28

static const char *LABEL[8] = {"", "OK", "Cancel", "&Abort", "&Retry", "&Ignore", "&Yes", "&No"};

/* ---- layout ---- */

static void far_text(const char *s, FARPTR *p, uint16_t *h) { *p = user_scratch((uint32_t)strlen(s) + 1, h); if (*p) strcpy(LIN(*p), s); }

static void measure_text(HDC16 dc, const char *s, int maxw, int *w, int *h) {
  uint16_t hs = 0, hr = 0;
  FARPTR ps, pr;
  RECT16 *r;
  far_text(s, &ps, &hs);
  pr = user_scratch(sizeof(RECT16), &hr);
  *w = *h = 0;
  if (ps && pr) {
    r = LIN(pr);
    *r = r_make(0, 0, maxw, 0);
    DrawText(dc, ps, -1, pr, DT_WORDBREAK | DT_CALCRECT | DT_NOPREFIX);
    r = LIN(pr);
    *w = r->right - r->left;
    *h = r->bottom - r->top;
  }
  user_scratch_free(hs);
  user_scratch_free(hr);
}

static int text_h(HDC16 dc) {
  int w, h;
  measure_text(dc, "Hg", 1000, &w, &h);
  return h;
}

/* label without the '&'; *amp is the index of the underlined character or -1 */
static void strip_amp(const char *in, char *out, int *amp) {
  int n = 0;
  *amp = -1;
  for (; *in; in++) {
    if (*in == '&' && in[1]) { *amp = n; continue; }
    out[n++] = *in;
  }
  out[n] = 0;
}

static void setup_buttons(UMsgBox *mb, uint16_t type) {
  static const uint8_t SET[6][3] = {{1, 0, 0}, {1, 2, 0}, {3, 4, 5}, {6, 7, 2}, {6, 7, 0}, {4, 2, 0}};
  static const uint8_t N[6] = {1, 2, 3, 3, 2, 2};
  unsigned t = type & MB_TYPEMASK;
  if (t > 5) t = 0;
  mb->nbtn = N[t];
  for (unsigned i = 0; i < mb->nbtn; i++) {
    mb->ids[i] = SET[t][i];
    strcpy(mb->labels[i], LABEL[SET[t][i]]);
  }
  mb->def = (uint8_t)((type & MB_DEFMASK) >> 8);
  if (mb->def >= mb->nbtn) mb->def = 0;
}

static void draw_label(HDC16 dc, RECT16 b, const char *label, bool pressed) {
  char buf[16];
  int amp, tw, th, x, y;
  uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
  strip_amp(label, buf, &amp);
  tw = user_text_width(dc, buf);
  th = text_h(dc);
  x = b.left + (b.right - b.left - tw) / 2 + (pressed ? 1 : 0);
  y = b.top + (b.bottom - b.top - th) / 2 + (pressed ? 1 : 0);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, COL(18));
  user_draw_text(dc, x, y, buf);
  if (amp >= 0) {
    char pre[16], one[2] = {buf[amp], 0};
    int pw;
    memcpy(pre, buf, (size_t)amp);
    pre[amp] = 0;
    pw = amp ? user_text_width(dc, pre) : 0;
    user_fill(dc, r_make(x + pw, y + th - 1, x + pw + user_text_width(dc, one), y + th), COL(18));
  }
  SelectObject(dc, old);
}

/* Push button: rounded black outline (two pixels for the default button), COLOR_BTNFACE with a
 * white highlight and a COLOR_BTNSHADOW shadow inside; pressed: shadow on top and left only. */
static void draw_button(HDC16 dc, RECT16 b, const char *label, bool def, bool pressed) {
  int t = def ? 2 : 1, l = b.left, tp = b.top, r = b.right, bt = b.bottom;
  uint32_t bg = COL(5);
  user_fill(dc, b, COL(6));
  user_fill(dc, r_make(l + t, tp + t, r - t, bt - t), COL(15));
  if (!pressed) {
    user_fill(dc, r_make(l + t, tp + t, r - t - 1, tp + t + 1), COL(20));
    user_fill(dc, r_make(l + t, tp + t, l + t + 1, bt - t - 1), COL(20));
    user_fill(dc, r_make(l + t + 1, bt - t - 1, r - t, bt - t), COL(16));
    user_fill(dc, r_make(r - t - 1, tp + t + 1, r - t, bt - t), COL(16));
  } else {
    user_fill(dc, r_make(l + t, tp + t, r - t, tp + t + 1), COL(16));
    user_fill(dc, r_make(l + t, tp + t, l + t + 1, bt - t), COL(16));
  }
  user_fill(dc, r_make(l, tp, l + 1, tp + 1), bg);            /* rounded corners */
  user_fill(dc, r_make(r - 1, tp, r, tp + 1), bg);
  user_fill(dc, r_make(l, bt - 1, l + 1, bt), bg);
  user_fill(dc, r_make(r - 1, bt - 1, r, bt), bg);
  draw_label(dc, b, label, pressed);
}

static void draw_buttons(HDC16 dc, const UMsgBox *mb) {
  for (unsigned i = 0; i < mb->nbtn; i++)
    draw_button(dc, mb->btn[i], mb->labels[i], i == mb->def, mb->pressed == i + 1 && mb->focus);
}

static void redraw_buttons(HWND16 h, const UMsgBox *mb) {
  HDC16 dc = GetDC(h);
  if (!dc) return;
  draw_buttons(dc, mb);
  ReleaseDC(h, dc);
}

/* ---- window procedure (class MB_CLASS, runtime callback segment offset 0x10) ---- */

static void finish(UMsgBox *mb, int id) {
  mb->result = (int16_t)id;
  mb->done = 1;
}

static void cancel(UMsgBox *mb) {
  for (unsigned i = 0; i < mb->nbtn; i++)
    if (mb->ids[i] == IDCANCEL) {
      finish(mb, IDCANCEL);
      return;
    }
  if (mb->nbtn == 1) finish(mb, mb->ids[0]);
  else MessageBeep(0);
}

static void move_def(HWND16 h, UMsgBox *mb, int d) {
  mb->def = (uint8_t)((mb->def + mb->nbtn + d) % mb->nbtn);
  redraw_buttons(h, mb);
}

static int button_at(const UMsgBox *mb, int x, int y) {
  for (unsigned i = 0; i < mb->nbtn; i++)
    if (r_contains(mb->btn[i], x, y)) return (int)i;
  return -1;
}

uint32_t user_msgbox_proc(HWND16 h, uint16_t msg, uint16_t wp, uint32_t lp) {
  UserState *st = user_state();
  UMsgBox *mb = &st->mb;
  int x = (int16_t)(lp & 0xFFFF), y = (int16_t)(lp >> 16);
  if (!mb->hwnd || h != mb->hwnd) return user_def_window_proc(h, msg, wp, lp);
  switch (msg) {
    case WM_PAINT: {
      uint16_t hp = 0, hr = 0;
      FARPTR pp = user_scratch(sizeof(PAINTSTRUCT16), &hp), pr = 0;
      HDC16 dc;
      if (!pp) return 0;
      dc = BeginPaint(h, pp);
      if (dc) {
        uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT)), hs = 0;
        FARPTR ps;
        if (mb->icon) DrawIcon(dc, mb->iconrc.left, mb->iconrc.top, mb->icon);
        far_text(mb->text, &ps, &hs);
        pr = user_scratch(sizeof(RECT16), &hr);
        if (ps && pr) {
          *(RECT16 *)LIN(pr) = mb->textrc;
          SetBkMode(dc, TRANSPARENT);
          SetTextColor(dc, COL(8));
          DrawText(dc, ps, -1, pr, DT_WORDBREAK | DT_NOPREFIX);
        }
        user_scratch_free(hs);
        user_scratch_free(hr);
        SelectObject(dc, old);
        draw_buttons(dc, mb);
      }
      EndPaint(h, pp);
      user_scratch_free(hp);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      int i = button_at(mb, x, y);
      if (i >= 0) {
        mb->pressed = (uint8_t)(i + 1);
        mb->focus = 1;
        mb->def = (uint8_t)i;
        SetCapture(h);
        redraw_buttons(h, mb);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (mb->pressed) {
        uint8_t in = button_at(mb, x, y) == mb->pressed - 1;
        if (in != mb->focus) {
          mb->focus = in;
          redraw_buttons(h, mb);
        }
      }
      return 0;
    case WM_LBUTTONUP:
      if (mb->pressed) {
        int i = mb->pressed - 1;
        bool inside = button_at(mb, x, y) == i;
        mb->pressed = 0;
        mb->focus = 0;
        if (GetCapture() == h) ReleaseCapture();
        redraw_buttons(h, mb);
        if (inside) finish(mb, mb->ids[i]);
      }
      return 0;
    case WM_KEYDOWN:
      switch (wp) {
        case VK_RETURN:
        case VK_SPACE: finish(mb, mb->ids[mb->def]); return 0;
        case VK_ESCAPE: cancel(mb); return 0;
        case VK_TAB: move_def(h, mb, (GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1); return 0;
        case VK_LEFT_:
        case VK_UP_: move_def(h, mb, -1); return 0;
        case VK_RIGHT_:
        case VK_DOWN_: move_def(h, mb, 1); return 0;
        default:
          for (unsigned i = 0; i < mb->nbtn; i++) {
            char buf[16];
            int amp;
            strip_amp(mb->labels[i], buf, &amp);
            if (amp >= 0 && (buf[amp] & ~0x20) == (int)(wp & ~0x20) && wp >= 'A') {
              finish(mb, mb->ids[i]);
              return 0;
            }
          }
          return 0;
      }
      return 0;
    case WM_CLOSE: cancel(mb); return 0;
    default: return user_def_window_proc(h, msg, wp, lp);
  }
}

static void ensure_class(void) {
  uint16_t hc = 0, hn = 0;
  FARPTR cp = user_scratch(sizeof(WNDCLASS16), &hc), np;
  WNDCLASS16 *wc;
  far_text(MB_CLASS, &np, &hn);
  if (cp && np) {
    wc = LIN(cp);
    memset(wc, 0, sizeof *wc);
    wc->lpfnWndProc = MK_FP(MEM_SEL_CALLBACK, 0x0010);
    wc->hbrBackground = 6; /* COLOR_WINDOW + 1 */
    wc->lpszClassName = np;
    RegisterClass(cp); /* 0 when already registered */
  }
  user_scratch_free(hc);
  user_scratch_free(hn);
}

bool user_msgbox_info(HWND16 *hwnd, int *nbtn, RECT16 btn[3], uint16_t ids[3], int *def, RECT16 *icon, RECT16 *text) {
  UserState *st = user_state();
  const UMsgBox *mb;
  const UWnd *w;
  int ox, oy;
  if (!st || !st->mb.hwnd || !(w = user_wnd(st->mb.hwnd))) return false;
  mb = &st->mb;
  user_client_origin(w, &ox, &oy);
  *hwnd = mb->hwnd;
  *nbtn = mb->nbtn;
  *def = mb->def;
  for (unsigned i = 0; i < mb->nbtn; i++) {
    btn[i] = r_offset(mb->btn[i], ox, oy);
    ids[i] = mb->ids[i];
  }
  *icon = r_offset(mb->iconrc, ox, oy);
  *text = r_offset(mb->textrc, ox, oy);
  return true;
}

/* True when h is a descendant of top (WS_CHILD parent chain). */
static bool in_tree(HWND16 top, HWND16 h) {
  for (unsigned guard = 0; guard < UW_MAX; guard++) {
    const UWnd *w = user_wnd(h);
    if (!w || h == user_desktop()) return false;
    if (h == top) return true;
    h = w->parent;
  }
  return false;
}

int16_t MessageBox(HWND16 hwndParent, FARPTR lpText, FARPTR lpCaption, uint16_t uType) {
  UserState *st = user_state();
  UMsgBox saved, *mb;
  UNcLayout L;
  HDC16 dc;
  HWND16 prev_focus = GetFocus(), h, owner = (hwndParent && user_wnd(hwndParent) && hwndParent != user_desktop()) ? hwndParent : 0;
  uint16_t hcls = 0, hcap = 0;
  FARPTR pcls, pcap = 0;
  int tw, th, cw, ch, iw = 0, bw = BTN_MINW, total, bx, by, W, H, textx, content_h, lw;
  char caption[64];
  uint32_t style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
  int16_t result;
  UPump pu;
  MSG16 m;
  if (!st) return 0;
  /* The owner is the top-level window: walk WS_CHILD parents up (Wine DIALOG_CreateIndirect; the game
   * passes GetFocus(), often a child). That window is disabled while the box is up and activated
   * afterwards. MB_TASKMODAL changes nothing further: with an owner it is disabled anyway, with
   * hwnd 0 nothing is disabled (Windows 3.1 would disable the task's windows; UNVERIFIED). */
  while (owner) {
    const UWnd *ow = user_wnd(owner);
    if (!ow || !(ow->style & WS_CHILD) || !user_wnd(ow->parent) || ow->parent == user_desktop()) break;
    owner = ow->parent;
  }
  saved = st->mb;
  mb = &st->mb;
  memset(mb, 0, sizeof *mb);
  mb->owner = owner;
  mb->type = uType;
  setup_buttons(mb, uType);
  {
    unsigned ic = uType & MB_ICONMASK;
    int id = ic == MB_ICONHAND ? IDI_HAND_ID : ic == MB_ICONQUESTION ? IDI_QUESTION_ID : ic == MB_ICONEXCLAMATION ? IDI_EXCLAMATION_ID
             : ic == MB_ICONASTERISK ? IDI_ASTERISK_ID : 0;
    mb->icon = id ? LoadIcon(0, (FARPTR)(uint32_t)id) : 0;
  }
  if (lpText && mem_check(lpText, 1)) {
    const char *s = LIN(lpText);
    size_t n = 0;
    while (n < sizeof mb->text - 1 && mem_check(lpText + (uint32_t)n, 1) && s[n]) { mb->text[n] = s[n]; n++; }
    mb->text[n] = 0;
  }
  strcpy(caption, "Error");
  if (lpCaption && mem_check(lpCaption, 1)) {
    const char *s = LIN(lpCaption);
    size_t n = 0;
    while (n < sizeof caption - 1 && mem_check(lpCaption + (uint32_t)n, 1) && s[n]) { caption[n] = s[n]; n++; }
    caption[n] = 0;
  }

  /* layout, client coordinates */
  dc = GetDC(0);
  {
    uint16_t old = SelectObject(dc, GetStockObject(SYSTEM_FONT));
    measure_text(dc, mb->text, TEXT_MAX - (mb->icon ? 48 : 0), &tw, &th);
    for (unsigned i = 0; i < mb->nbtn; i++) {
      char buf[16];
      int amp;
      strip_amp(mb->labels[i], buf, &amp);
      lw = user_text_width(dc, buf) + 24;
      if (lw > bw) bw = lw;
    }
    SelectObject(dc, old);
  }
  DeleteDC(dc);
  if (mb->icon) iw = 32 + 16;
  content_h = th > 32 && mb->icon ? th : (mb->icon ? 32 : th);
  total = (int)mb->nbtn * bw + ((int)mb->nbtn - 1) * BTN_GAP;
  cw = ML + iw + tw + ML;
  if (cw < total + 2 * ML) cw = total + 2 * ML;
  ch = MT + content_h + 20 + BTN_H + 16;
  textx = ML + iw;
  if (mb->icon) mb->iconrc = r_make(ML, MT, ML + 32, MT + 32);
  mb->textrc = r_make(textx, MT + (mb->icon && th < 32 ? (32 - th) / 2 : 0), textx + tw + 2, MT + (mb->icon && th < 32 ? (32 - th) / 2 : 0) + th);
  bx = (cw - total) / 2;
  by = MT + content_h + 20;
  for (unsigned i = 0; i < mb->nbtn; i++) mb->btn[i] = r_make(bx + (int)i * (bw + BTN_GAP), by, bx + (int)i * (bw + BTN_GAP) + bw, by + BTN_H);

  user_nc_layout_for(style, WS_EX_DLGMODALFRAME, 1000, 1000, &L);
  W = cw + L.client.left + (1000 - L.client.right);
  H = ch + L.client.top + (1000 - L.client.bottom);

  ensure_class();
  far_text(MB_CLASS, &pcls, &hcls);
  far_text(caption, &pcap, &hcap);
  h = user_create_window_ex(WS_EX_DLGMODALFRAME, pcls, pcap, style, (int16_t)((GDI_SCREEN_W - W) / 2), (int16_t)((GDI_SCREEN_H - H) / 2),
                            (int16_t)W, (int16_t)H, owner, 0, 0, 0);
  user_scratch_free(hcls);
  user_scratch_free(hcap);
  if (!h) {
    st->mb = saved;
    return 0;
  }
  mb->hwnd = h;
  if (owner) EnableWindow(owner, 0);
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
  SetFocus(h);
  if (user_pump_open(&pu)) {
    while (!st->mb.done && user_wnd(h) && user_pump_get(&pu, &m)) {
      TranslateMessage(pu.p);
      DispatchMessage(pu.p);
    }
    user_pump_close(&pu);
  }
  result = st->mb.done ? st->mb.result : 0;
  if (owner && user_wnd(owner)) EnableWindow(owner, 1);
  if (user_wnd(h)) DestroyWindow(h);
  if (owner && user_wnd(owner)) SetActiveWindow(owner);
  /* the window that had the focus before the box gets it back when it is inside the owner's tree */
  if (owner && prev_focus && prev_focus != owner && user_wnd(prev_focus) && in_tree(owner, prev_focus) && IsWindowEnabled(prev_focus))
    SetFocus(prev_focus);
  st->mb = saved;
  return result;
}
