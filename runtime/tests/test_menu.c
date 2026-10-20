/* Expectations for menus and accelerators (task stars-0z4.13): STARSMENU and the two
 * ACCELERATOR resources of original/stars26i.exe, the menu API, the menu bar in the non-client
 * area, pop-up menus, TrackPopupMenu, keyboard and mouse tracking, the system menu pop-up and
 * LoadAccelerators / TranslateAccelerator. Written BEFORE runtime/menu.c, menu_ui.c and accel.c.
 * Do not edit an expectation to match the code: a disagreement is reported with both values.
 *
 * Sources. No Windows 3.1 SDK text and no reference capture (tests/ref/ is absent) exist on this
 * machine, so everything marked UNVERIFIED below is NOT confirmed against Windows 3.1.
 *   RESOURCES  RT_MENU "STARSMENU" and RT_ACCELERATOR 116 and 1080 of the original image, read
 *              through FindResource / LoadResource (runtime/kernel.c over runtime/ne.h) and pinned
 *              against build/nedump --dump-resources (re/resources/0059-menu-STARSMENU.txt,
 *              0096-accelerator-116.bin, 0097-accelerator-1080.bin; the menu text is compared line
 *              by line when the file is present). Fixed facts: 6 top-level pop-ups with 11, 9, 2,
 *              6, 9 and 7 items; table 116 has 18 entries, table 1080 has 4.
 *   WINE       wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0 (lines cited in
 *              runtime/menu.h): dlls/win32u/menu.c, dlls/win32u/defwnd.c, dlls/user.exe16/user.c
 *              and message.c, include/winuser.rh, include/winuser.h.
 *   GAME       re/decomp/seg04/467c_HOSTMODEDIALOG.c:93-96: TrackPopupMenu(... flags (0|2)|4), then
 *              DestroyMenu and PeekMessage(WM_COMMAND, WM_COMMAND, PM_NOYIELD = 2, no removal): the command is
 *              POSTED by the tracker.
 *
 * UNVERIFIED (also listed in docs/inventory.md section 18):
 *   M1  Menu bar height = SM_CYMENU = 19: 18 rows of items and a 1-pixel COLOR_WINDOWFRAME line.
 *   M2  Bar item width = text width + 16, text at +8, the first item at the left edge of the bar.
 *   M3  Pop-up: 1-pixel COLOR_WINDOWFRAME border, no shadow; items 18 high; separator 9 high with a
 *       COLOR_WINDOWFRAME line; a 16-pixel check column; text after a tab in a second column.
 *   M4  Colours: COLOR_MENU, COLOR_MENUTEXT, selected item COLOR_HIGHLIGHT / COLOR_HIGHLIGHTTEXT,
 *       grayed text COLOR_GRAYTEXT.
 *   M5  Pop-up position: left aligned at x, top at y, moved to stay on the screen.
 *   M6  The bar never wraps to a second row.
 *   M7  Two presses on the control-menu box within 500 ms close the window.
 *   M8  The flags of WM_MENUSELECT include MF_HILITE (the item state), as Wine's select_item does.
 *   M9  A right-button release outside every menu does not end TrackPopupMenu (Wine 4207-4224).
 */
#include "test.h"

#include <stdarg.h>
#include <sys/stat.h>

#include "user_test_common.h"
#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/user_internal.h"

#define CLS "MENUCLS"
#define TOPW (WS_OVERLAPPEDWINDOW | WS_VISIBLE)
#define WX 20
#define WY 30
#define WW 500
#define WH 300

#define K_END 0x23
#define K_HOME 0x24
#define K_LEFT 0x25
#define K_UP 0x26
#define K_RIGHT 0x27
#define K_DOWN 0x28

static uint8_t *g_img;
static HANDLE16 g_hinst;
static HWND16 g_top;

/* ---- scripted input: one step is fed each time GetMessage has nothing else to return ---- */

typedef struct Step {
  char kind; /* 'D' key down, 'U' key up, 'T' tap, 'M' mouse (x, y, b), 'I' mouse onto item (menu x, pos y, b), 'F' call fn */
  int x, y, b;
  void (*fn)(void);
} Step;
static Step g_script[64];
static int g_nscript, g_iscript;
static uint32_t g_t = 5000;
static uint32_t g_dt = 600;

static void s_clear(void) { g_nscript = g_iscript = 0; }
static void s_down(int vk) { g_script[g_nscript++] = (Step){'D', vk, 0, 0, 0}; }
static void s_up(int vk) { g_script[g_nscript++] = (Step){'U', vk, 0, 0, 0}; }
static void s_tap(int vk) { g_script[g_nscript++] = (Step){'T', vk, 0, 0, 0}; }
static void s_mouse(int x, int y, int b) { g_script[g_nscript++] = (Step){'M', x, y, b, 0}; }
static void s_item(HMENU16 m, unsigned pos, int b) { g_script[g_nscript++] = (Step){'I', m, (int)pos, b, 0}; }
static void s_fn(void (*fn)(void)) { g_script[g_nscript++] = (Step){'F', 0, 0, 0, fn}; }

static bool item_center(HMENU16 m, unsigned pos, int *x, int *y) {
  RECT16 r;
  if (!user_menu_item_rect(m, pos, &r)) return false;
  *x = (r.left + r.right) / 2;
  *y = (r.top + r.bottom) / 2;
  return true;
}

static bool feeder(void) {
  const Step *s;
  if (g_iscript >= g_nscript) return false;
  s = &g_script[g_iscript++];
  g_t += g_dt;
  switch (s->kind) {
    case 'D': rt_input_key((uint16_t)s->x, 0x01, 1, g_t); break;
    case 'U': rt_input_key((uint16_t)s->x, 0x01, 0, g_t); break;
    case 'T':
      rt_input_key((uint16_t)s->x, 0x01, 1, g_t);
      rt_input_key((uint16_t)s->x, 0x01, 0, g_t + 1);
      break;
    case 'M': rt_input_mouse((int16_t)s->x, (int16_t)s->y, (uint16_t)s->b, g_t); break;
    case 'I': {
      int x = 0, y = 0;
      CHECK(item_center((HMENU16)s->x, (unsigned)s->y, &x, &y));
      rt_input_mouse((int16_t)x, (int16_t)y, (uint16_t)s->b, g_t);
      break;
    }
    default: s->fn(); break;
  }
  return true;
}

/* The application loop; a non-zero g_accel_loop makes it call TranslateAccelerator like a game does. */
static HACCEL16 g_accel_loop;
static void run_script(void) {
  FARPTR p = far_alloc(sizeof(MSG16));
  int guard = 0;
  g_iscript = 0;
  user_wait_hook = feeder;
  while (guard++ < 800 && GetMessage(p, 0, 0, 0)) {
    if (g_accel_loop && g_top && TranslateAccelerator(g_top, g_accel_loop, p)) continue;
    TranslateMessage(p);
    DispatchMessage(p);
  }
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  s_clear();
}

/* ---- helpers ---- */

static uint8_t PXL(int x, int y) { return fb_pixels()[y * GDI_SCREEN_W + x]; }
static uint8_t CI(unsigned color) {
  static const int idx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
  uint32_t want = GetSysColor((uint16_t)color);
  for (int k = 0; k < 20; k++)
    if (fb_palette_entry((unsigned)idx[k]) == want) return (uint8_t)idx[k];
  return 0;
}
#define C_MENU CI(4)
#define C_MENUTEXT CI(7)
#define C_FRAME CI(6)
#define C_HILITE CI(13)
#define C_HILITETEXT CI(14)
#define C_GRAYTEXT CI(17)

static int count_px(int l, int t, int r, int b, int c) {
  int n = 0;
  for (int y = t; y < b; y++)
    for (int x = l; x < r; x++) n += PXL(x, y) == c;
  return n;
}

static void dump_frame(const char *name, int x, int y, int w, int h) {
  static uint32_t rgba[GDI_SCREEN_W * GDI_SCREEN_H];
  char path[160];
  FILE *f;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x + w > GDI_SCREEN_W) w = GDI_SCREEN_W - x;
  if (y + h > GDI_SCREEN_H) h = GDI_SCREEN_H - y;
  mkdir("re", 0755);
  mkdir("re/frames", 0755);
  snprintf(path, sizeof path, "re/frames/%s.ppm", name);
  f = fopen(path, "wb");
  if (!f) return;
  fb_present(rgba);
  fprintf(f, "P6\n%d %d\n255\n", w, h);
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) {
      uint32_t c = rgba[(y + j) * GDI_SCREEN_W + x + i];
      unsigned char px3[3] = {(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255)};
      fwrite(px3, 1, 3, f);
    }
  fclose(f);
}

static HMENU16 load_stars_menu(void) {
  HMENU16 h = LoadMenu(g_hinst, fstr("STARSMENU"));
  CHECK(h != 0);
  return h;
}

static HWND16 make_frame(HMENU16 hm) {
  HWND16 h;
  next_label("T");
  h = CreateWindow(fstr(CLS), fstr("Stars"), TOPW, WX, WY, WW, WH, 0, hm, g_hinst, 0);
  CHECK(h != 0);
  bind_label(h, "T");
  g_top = h;
  g_accel_loop = 0;
  log_clear();
  return h;
}

static void kill_frame(void) {
  if (g_top && IsWindow(g_top)) DestroyWindow(g_top);
  g_top = 0;
  g_accel_loop = 0;
}

/* The menu messages of the window "T", as one string. IM = WM_INITMENU (wParam/lParam), IP =
 * WM_INITMENUPOPUP (wParam/LOWORD/HIWORD of lParam), MS = WM_MENUSELECT (wParam/LOWORD/HIWORD),
 * CMD = WM_COMMAND (id/lParam), SYS = WM_SYSCOMMAND, MC = WM_MENUCHAR (char/lParam). Hex. */
static const char *trace(void) {
  static char out[4096];
  size_t n = 0;
  out[0] = 0;
  for (int i = 0; i < g_nlog; i++) {
    const LogEnt *e = &g_log[i];
    const char *sep = n ? " " : "";
    if (strcmp(label_of(e->hwnd), "T") != 0) continue;
    switch (e->msg) {
      case WM_INITMENU: n += (size_t)snprintf(out + n, sizeof out - n, "%sIM:%04X/%lX", sep, e->wp, (unsigned long)e->lp); break;
      case WM_INITMENUPOPUP:
        n += (size_t)snprintf(out + n, sizeof out - n, "%sIP:%04X/%X/%X", sep, e->wp, (unsigned)(e->lp & 0xFFFF), (unsigned)(e->lp >> 16));
        break;
      case WM_MENUSELECT:
        n += (size_t)snprintf(out + n, sizeof out - n, "%sMS:%04X/%04X/%04X", sep, e->wp, (unsigned)(e->lp & 0xFFFF), (unsigned)(e->lp >> 16));
        break;
      case WM_COMMAND: n += (size_t)snprintf(out + n, sizeof out - n, "%sCMD:%u/%lX", sep, e->wp, (unsigned long)e->lp); break;
      case WM_SYSCOMMAND: n += (size_t)snprintf(out + n, sizeof out - n, "%sSYS:%04X", sep, e->wp); break;
      case WM_MENUCHAR: n += (size_t)snprintf(out + n, sizeof out - n, "%sMC:%02X/%lX", sep, e->wp, (unsigned long)e->lp); break;
      default: break;
    }
    if (n >= sizeof out - 80) break;
  }
  return out;
}
#define CHECK_TRACE(want)                                                                                  \
  do {                                                                                                     \
    const char *want_ = (want);                                                                            \
    const char *got_ = trace();                                                                            \
    test_checks_run++;                                                                                     \
    if (strcmp(got_, want_) != 0) {                                                                        \
      test_checks_failed++;                                                                                \
      fprintf(stderr, "%s:%d: messages differ\n  got:  %s\n  want: %s\n", __FILE__, __LINE__, got_, want_); \
    }                                                                                                      \
  } while (0)

static char g_want[2048];
static const char *fm(const char *f, ...) __attribute__((format(printf, 1, 2)));
static const char *fm(const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  vsnprintf(g_want, sizeof g_want, f, ap);
  va_end(ap);
  return g_want;
}

/* ---- tests ---- */

static void test_constants(void) {
  CHECK_EQ(WM_COMMAND, 0x0111);
  CHECK_EQ(WM_INITMENU, 0x0116);
  CHECK_EQ(WM_INITMENUPOPUP, 0x0117);
  CHECK_EQ(WM_MENUSELECT, 0x011F);
  CHECK_EQ(WM_MENUCHAR, 0x0120);
  CHECK_EQ(WM_ENTERIDLE, 0x0121);
  CHECK_EQ(MF_GRAYED, 0x0001);
  CHECK_EQ(MF_DISABLED, 0x0002);
  CHECK_EQ(MF_CHECKED, 0x0008);
  CHECK_EQ(MF_POPUP, 0x0010);
  CHECK_EQ(MF_END, 0x0080);
  CHECK_EQ(MF_HILITE, 0x0080);
  CHECK_EQ(MF_BYPOSITION, 0x0400);
  CHECK_EQ(MF_SEPARATOR, 0x0800);
  CHECK_EQ(MF_SYSMENU, 0x2000);
  CHECK_EQ(TPM_LEFTBUTTON, 0);
  CHECK_EQ(TPM_RIGHTBUTTON, 2);
  CHECK_EQ(TPM_CENTERALIGN, 4);
  CHECK_EQ(TPM_RIGHTALIGN, 8);
  CHECK_EQ(FVIRTKEY, 1);
  CHECK_EQ(FNOINVERT, 2);
  CHECK_EQ(FSHIFT, 4);
  CHECK_EQ(FCONTROL, 8);
  CHECK_EQ(FALT, 0x10);
  CHECK_EQ(GetSystemMetrics(15), 19); /* SM_CYMENU, UNVERIFIED (M1) */
}

/* One level of a menu as the text nedump prints it (2 spaces of indent per level). */
static void dump_menu(HMENU16 m, int depth, char *out, size_t cap, size_t *n) {
  int cnt = GetMenuItemCount(m);
  for (int i = 0; i < cnt; i++) {
    UMenuItemInfo it;
    unsigned fl;
    CHECK(user_menu_item_info(m, (unsigned)i, &it));
    fl = it.flags & ~(unsigned)MF_SEPARATOR;
    if (i == cnt - 1) fl |= MF_END;
    if (it.flags & MF_POPUP) {
      *n += (size_t)snprintf(out + *n, cap - *n, "%*sPOPUP \"%s\" flags=0x%04x\n", depth * 2, "", it.text, fl);
      dump_menu(it.sub, depth + 1, out, cap, n);
    } else {
      *n += (size_t)snprintf(out + *n, cap - *n, "%*sITEM id=%u \"%s\" flags=0x%04x\n", depth * 2, "", it.id, it.text, fl);
    }
  }
}

static void test_starsmenu_resource(void) {
  static char got[8192];
  static const char head[] = "MENU version=0\n";
  size_t n = 0;
  HMENU16 bar = load_stars_menu();
  static const char *tops[6] = {"&File", "&View", "&Turn", "&Commands", "&Report", "&Help"};
  static const int counts[6] = {11, 9, 2, 6, 9, 7};
  CHECK_EQ(GetMenuItemCount(bar), 6);
  for (int i = 0; i < 6; i++) {
    UMenuItemInfo it;
    HMENU16 sub = GetSubMenu(bar, (int16_t)i);
    CHECK(user_menu_item_info(bar, (unsigned)i, &it));
    CHECK(strcmp(it.text, tops[i]) == 0);
    CHECK(it.flags & MF_POPUP);
    CHECK(sub != 0 && sub == it.sub);
    CHECK_EQ(GetMenuItemCount(sub), counts[i]);
  }
  {
    UMenuItemInfo it;
    HMENU16 file = GetSubMenu(bar, 0), view = GetSubMenu(bar, 1), turn = GetSubMenu(bar, 2), report = GetSubMenu(bar, 4);
    HMENU16 zoom = GetSubMenu(view, 3), layout = GetSubMenu(view, 4), dump = GetSubMenu(report, 8);
    CHECK(user_menu_item_info(file, 0, &it));
    CHECK(strcmp(it.text, "&New...\tCtrl+N") == 0);
    CHECK_EQ(it.id, 3800);
    CHECK_EQ(it.flags, 0);
    CHECK(user_menu_item_info(file, 6, &it)); /* separator */
    CHECK(it.flags & MF_SEPARATOR);
    CHECK(user_menu_item_info(file, 10, &it));
    CHECK(strcmp(it.text, "E&xit") == 0);
    CHECK_EQ(it.id, 3810);
    CHECK(user_menu_item_info(turn, 0, &it));
    CHECK_EQ(it.id, 106);
    CHECK(it.flags & MF_GRAYED); /* "&Wait for New": resource flags 0x0001 */
    CHECK(user_menu_item_info(turn, 1, &it));
    CHECK_EQ(it.id, 105);
    CHECK(it.flags & MF_GRAYED); /* "&Generate": 0x0081 = grayed + last */
    CHECK_EQ(GetMenuItemCount(zoom), 9);
    CHECK(user_menu_item_info(zoom, 4, &it));
    CHECK(strcmp(it.text, "100%") == 0);
    CHECK_EQ(it.id, 3905);
    CHECK_EQ(GetMenuItemCount(layout), 3);
    CHECK_EQ(GetMenuItemCount(dump), 3);
    CHECK(user_menu_item_info(dump, 2, &it));
    CHECK_EQ(it.id, 83);
    CHECK(user_menu_item_info(view, 0, &it));
    CHECK(strcmp(it.text, "&Toolbar") == 0);
    CHECK_EQ(it.id, 179);
  }
  /* the whole tree against the text nedump prints for the resource */
  memcpy(got, head, sizeof head - 1);
  dump_menu(bar, 1, got + sizeof head - 1, sizeof got - sizeof head, &n); /* nedump indents the top level by two */
  got[sizeof head - 1 + n] = 0;
  {
    FILE *f = fopen("re/resources/0059-menu-STARSMENU.txt", "rb");
    if (f) {
      static char want[8192];
      size_t m = fread(want, 1, sizeof want - 1, f);
      fclose(f);
      want[m] = 0;
      CHECK(strcmp(got, want) == 0);
      if (strcmp(got, want) != 0) fprintf(stderr, "---- got ----\n%s---- want ----\n%s", got, want);
    } else {
      fprintf(stderr, "test_menu: re/resources/0059-menu-STARSMENU.txt absent, the nedump comparison is skipped\n");
    }
  }
  /* resources that do not exist */
  CHECK_EQ(LoadMenu(g_hinst, fstr("NOSUCHMENU")), 0);
  CHECK_EQ(LoadMenu(g_hinst, MK_FP(0, 31999)), 0);
  CHECK(DestroyMenu(bar));
  CHECK_EQ(GetMenuItemCount(bar), -1);
}

static void test_accel_tables(void) {
  UAccelEntry e[32];
  HACCEL16 a116 = LoadAccelerators(g_hinst, MK_FP(0, 116)), a1080 = LoadAccelerators(g_hinst, MK_FP(0, 1080));
  unsigned n;
  CHECK(a116 != 0 && a1080 != 0 && a116 != a1080);
  n = user_accel_entries(a116, e, 32);
  CHECK_EQ(n, 18);
  /* pinned from re/resources/0096-accelerator-116.bin (flag 0x80 = last entry, not part of fVirt here) */
  CHECK_EQ(e[0].fVirt, 0x0B);
  CHECK_EQ(e[0].key, 0x41);
  CHECK_EQ(e[0].cmd, 1064);
  CHECK_EQ(e[1].key, 0x4E);
  CHECK_EQ(e[1].cmd, 110);
  CHECK_EQ(e[4].key, 0x46);
  CHECK_EQ(e[4].cmd, 4201);
  CHECK_EQ(e[5].fVirt, 0x03);
  CHECK_EQ(e[5].key, 0x70);
  CHECK_EQ(e[5].cmd, 138);
  CHECK_EQ(e[14].key, 0x79);
  CHECK_EQ(e[14].cmd, 96);
  CHECK_EQ(e[15].cmd, 21000);
  CHECK_EQ(e[16].fVirt, 0x0B);
  CHECK_EQ(e[16].cmd, 21001);
  CHECK_EQ(e[17].fVirt, 0x07);
  CHECK_EQ(e[17].key, 0x7B);
  CHECK_EQ(e[17].cmd, 21002);
  n = user_accel_entries(a1080, e, 32);
  CHECK_EQ(n, 4);
  CHECK_EQ(e[0].fVirt, 0x13);
  CHECK_EQ(e[0].key, 0x4E);
  CHECK_EQ(e[0].cmd, 250);
  CHECK_EQ(e[1].cmd, 251);
  CHECK_EQ(e[2].key, 0x43);
  CHECK_EQ(e[2].cmd, 252);
  CHECK_EQ(e[3].fVirt, 0x13);
  CHECK_EQ(e[3].key, 0x58);
  CHECK_EQ(e[3].cmd, 253);
  CHECK_EQ(LoadAccelerators(g_hinst, MK_FP(0, 117)), 0);
  CHECK_EQ(LoadAccelerators(g_hinst, fstr("NOSUCHACCEL")), 0);
  CHECK_EQ(user_accel_entries(0x1FFF, e, 32), 0);
}

static void test_menu_api(void) {
  HMENU16 m = CreatePopupMenu(), sub = CreatePopupMenu();
  UMenuItemInfo it;
  CHECK(m != 0 && sub != 0 && m != sub);
  CHECK_EQ(GetMenuItemCount(m), 0);
  CHECK_EQ(GetMenuItemCount(0x1FFF), -1);
  CHECK(AppendMenu(m, MF_STRING, 1, fstr("One")));
  CHECK(AppendMenu(m, MF_STRING | MF_GRAYED, 2, fstr("Two\tCtrl+2")));
  CHECK(AppendMenu(m, MF_SEPARATOR, 0, 0));
  CHECK(AppendMenu(m, MF_POPUP, sub, fstr("Sub")));
  CHECK(AppendMenu(sub, MF_STRING, 30, fstr("Thirty")));
  CHECK_EQ(GetMenuItemCount(m), 4);
  CHECK(user_menu_item_info(m, 1, &it));
  CHECK_EQ(it.id, 2);
  CHECK(it.flags & MF_GRAYED);
  CHECK(strcmp(it.text, "Two\tCtrl+2") == 0);
  CHECK(user_menu_item_info(m, 2, &it));
  CHECK(it.flags & MF_SEPARATOR);
  CHECK(user_menu_item_info(m, 3, &it));
  CHECK(it.flags & MF_POPUP);
  CHECK_EQ(it.sub, sub);
  CHECK_EQ(GetSubMenu(m, 3), sub);
  CHECK_EQ(GetSubMenu(m, 0), 0);
  CHECK_EQ(GetSubMenu(m, 9), 0);
  /* InsertMenu by position and by command; position 0xFFFF appends */
  CHECK(InsertMenu(m, 0, MF_BYPOSITION | MF_STRING, 9, fstr("First")));
  CHECK(user_menu_item_info(m, 0, &it));
  CHECK_EQ(it.id, 9);
  CHECK_EQ(GetMenuItemCount(m), 5);                         /* First One Two sep Sub */
  CHECK(InsertMenu(m, 2, MF_BYCOMMAND | MF_STRING, 7, fstr("Before Two")));
  CHECK(user_menu_item_info(m, 2, &it));                    /* First One Before-Two Two sep Sub: id 2 was at 2 */
  CHECK_EQ(it.id, 7);
  CHECK(InsertMenu(m, 0xFFFF, MF_BYPOSITION | MF_STRING, 8, fstr("Last")));
  CHECK_EQ(GetMenuItemCount(m), 7);
  CHECK(user_menu_item_info(m, 6, &it));
  CHECK_EQ(it.id, 8);
  CHECK(!InsertMenu(0x1FFF, 0, MF_BYPOSITION, 1, fstr("x")));
  CHECK(!AppendMenu(0x1FFF, MF_STRING, 1, fstr("x")));
  /* CheckMenuItem returns the previous state; by command it also finds items in sub-menus */
  CHECK_EQ(CheckMenuItem(m, 1, MF_BYCOMMAND | MF_CHECKED), MF_UNCHECKED);
  CHECK_EQ(CheckMenuItem(m, 1, MF_BYCOMMAND | MF_CHECKED), MF_CHECKED);
  CHECK_EQ(GetMenuState(m, 1, MF_BYCOMMAND) & MF_CHECKED, MF_CHECKED);
  CHECK_EQ(CheckMenuItem(m, 1, MF_BYCOMMAND | MF_UNCHECKED), MF_CHECKED);
  CHECK_EQ(GetMenuState(m, 1, MF_BYCOMMAND) & MF_CHECKED, 0);
  CHECK_EQ(CheckMenuItem(m, 30, MF_BYCOMMAND | MF_CHECKED), MF_UNCHECKED);
  CHECK(user_menu_item_info(sub, 0, &it));
  CHECK(it.flags & MF_CHECKED);
  CHECK_EQ(CheckMenuItem(m, 0, MF_BYPOSITION | MF_CHECKED), MF_UNCHECKED); /* position 0 = id 9 */
  CHECK(user_menu_item_info(m, 0, &it));
  CHECK(it.flags & MF_CHECKED);
  CHECK_EQ(CheckMenuItem(m, 999, MF_BYCOMMAND | MF_CHECKED), 0xFFFF);
  CHECK_EQ(CheckMenuItem(m, 99, MF_BYPOSITION | MF_CHECKED), 0xFFFF);
  /* EnableMenuItem returns the previous grayed / disabled state */
  CHECK_EQ(EnableMenuItem(m, 1, MF_BYCOMMAND | MF_GRAYED), MF_ENABLED);
  CHECK_EQ(EnableMenuItem(m, 1, MF_BYCOMMAND | MF_DISABLED), MF_GRAYED);
  CHECK_EQ(GetMenuState(m, 1, MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED), MF_DISABLED);
  CHECK_EQ(EnableMenuItem(m, 1, MF_BYCOMMAND | MF_ENABLED), MF_DISABLED);
  CHECK_EQ(GetMenuState(m, 1, MF_BYCOMMAND) & (MF_GRAYED | MF_DISABLED), 0);
  CHECK_EQ(EnableMenuItem(m, 777, MF_BYCOMMAND | MF_GRAYED), 0xFFFF);
  CHECK_EQ(GetMenuState(m, 777, MF_BYCOMMAND), 0xFFFF);
  /* GetMenuState of a pop-up item: item count in the high byte */
  CHECK_EQ(GetMenuState(m, 5, MF_BYPOSITION), (1u << 8) | MF_POPUP);
  /* DeleteMenu destroys a pop-up's sub-menu, RemoveMenu keeps it */
  CHECK(DeleteMenu(m, 7, MF_BYCOMMAND));
  CHECK_EQ(GetMenuItemCount(m), 6);
  CHECK(!DeleteMenu(m, 7, MF_BYCOMMAND));
  CHECK(!DeleteMenu(m, 50, MF_BYPOSITION));
  {
    HMENU16 keep = CreatePopupMenu();
    unsigned last;
    CHECK(AppendMenu(m, MF_POPUP, keep, fstr("Keep")));
    last = (unsigned)GetMenuItemCount(m) - 1;
    CHECK(RemoveMenu(m, (uint16_t)last, MF_BYPOSITION));
    CHECK_EQ(GetMenuItemCount(keep), 0); /* still a valid menu */
    CHECK(DestroyMenu(keep));
  }
  CHECK(user_menu_item_info(m, 4, &it)); /* First One Two sep Sub Last: Sub */
  CHECK(it.flags & MF_POPUP);
  CHECK(DeleteMenu(m, 4, MF_BYPOSITION));
  CHECK_EQ(GetMenuItemCount(sub), -1);
  CHECK(DestroyMenu(m));
  CHECK(!DestroyMenu(m));
  CHECK_EQ(GetMenuItemCount(m), -1);
}

static void test_menu_in_window(void) {
  HMENU16 bar = load_stars_menu();
  HWND16 plain, h;
  UNcLayout L;
  RECT16 cr, cr0;
  plain = mk("T", CLS, "Plain", TOPW, WX, WY, WW, WH, 0, 0);
  CHECK(plain != 0);
  CHECK_EQ(GetMenu(plain), 0);
  cr0 = get_rect(plain, 1);
  DestroyWindow(plain);
  h = make_frame(bar);
  CHECK_EQ(GetMenu(h), bar);
  cr = get_rect(h, 1);
  /* issue dependency (2): the client area is SM_CYMENU shorter */
  CHECK_EQ(cr.right - cr.left, cr0.right - cr0.left);
  CHECK_EQ(cr.bottom - cr.top, (cr0.bottom - cr0.top) - GetSystemMetrics(15));
  CHECK(user_nc_layout(h, &L));
  CHECK_RECT(L.menubar, 4, 24, WW - 4, 24 + 19);
  CHECK_EQ(L.client.top, 24 + 19);
  /* HTMENU is hit-tested */
  CHECK_EQ(SendMessage(h, WM_NCHITTEST, 0, pt32(WX + 50, WY + 24 + 3)), HTMENU);
  CHECK_EQ(SendMessage(h, WM_NCHITTEST, 0, pt32(WX + 50, WY + 24 + 18)), HTMENU);
  CHECK_EQ(SendMessage(h, WM_NCHITTEST, 0, pt32(WX + 50, WY + 24 + 19)), HTCLIENT);
  CHECK_EQ(SendMessage(h, WM_NCHITTEST, 0, pt32(WX + 50, WY + 22)), HTCAPTION);
  /* AdjustWindowRect with a menu is consistent with the layout */
  {
    FARPTR p = far_alloc(sizeof(RECT16));
    RECT16 *r = LIN(p);
    *r = (RECT16){0, 0, 100, 100};
    AdjustWindowRect(p, WS_OVERLAPPEDWINDOW, 1);
    CHECK_EQ(r->bottom - r->top, 100 + 8 + 20 + 19);
  }
  /* SetMenu attaches and detaches a bar and recalculates the client area */
  {
    HWND16 q = mk("Q", CLS, "NoMenu", TOPW, 300, 100, 200, 150, 0, 0);
    RECT16 a, b;
    CHECK(q != 0);
    a = get_rect(q, 1);
    CHECK(SetMenu(q, CreateMenu()));
    b = get_rect(q, 1);
    CHECK_EQ(b.bottom - b.top, (a.bottom - a.top) - 19);
    CHECK(SetMenu(q, 0));
    b = get_rect(q, 1);
    CHECK_EQ(b.bottom - b.top, a.bottom - a.top);
    DestroyWindow(q);
  }
  /* a child window has no menu */
  {
    HWND16 c = mk("C", "BUTTON", "b", WS_CHILD | WS_VISIBLE, 5, 5, 50, 20, h, 77);
    CHECK(c != 0);
    CHECK_EQ(GetMenu(c), 0);
    DestroyWindow(c);
  }
  /* destroying the window destroys its menu */
  kill_frame();
  CHECK_EQ(GetMenuItemCount(bar), -1);
}

/* A class with lpszMenuName gives each new top-level window a menu loaded from the class's module (the game
 * imports neither LoadMenu nor SetMenu); a menu handle in CreateWindow's hMenu wins over it. */
static void test_class_menu(void) {
  FARPTR p = far_alloc(sizeof(WNDCLASS16));
  WNDCLASS16 *wc = LIN(p);
  HWND16 h, h2;
  RECT16 a, b;
  HMENU16 own = CreateMenu();
  memset(wc, 0, sizeof *wc);
  wc->lpfnWndProc = FP_PROC_A;
  wc->lpszClassName = fstr("MENUCLS2");
  wc->lpszMenuName = fstr("STARSMENU");
  wc->hInstance = g_hinst;
  CHECK(RegisterClass(p) != 0);
  h = mk("C1", "MENUCLS2", "class menu", TOPW, 100, 100, 300, 200, 0, 0);
  CHECK(h != 0);
  CHECK(GetMenu(h) != 0);
  CHECK_EQ(GetMenuItemCount(GetMenu(h)), 6);
  a = get_rect(h, 1);
  h2 = mk("C2", "MENUCLS2", "own menu", TOPW, 100, 100, 300, 200, 0, own);
  CHECK(h2 != 0);
  CHECK_EQ(GetMenu(h2), own);
  CHECK_EQ(GetMenuItemCount(own), 0);
  b = get_rect(h2, 1);
  CHECK_EQ(a.bottom - a.top, b.bottom - b.top); /* both have a bar */
  CHECK(GetMenu(h) != GetMenu(h2));             /* each window loads its own copy of the class menu */
  DestroyWindow(h);
  DestroyWindow(h2);
}

static void test_bar_layout(void) {
  HMENU16 bar = load_stars_menu();
  RECT16 prev = {0, 0, 0, 0}, r;
  make_frame(bar);
  for (unsigned i = 0; i < 6; i++) {
    CHECK(user_menu_item_rect(bar, i, &r));
    CHECK_EQ(r.top, WY + 24);
    CHECK_EQ(r.bottom, WY + 24 + 18);
    CHECK(r.right > r.left + 16);
    if (i == 0) CHECK_EQ(r.left, WX + 4); /* M2 */
    else CHECK_EQ(r.left, prev.right);
    prev = r;
  }
  CHECK(!user_menu_item_rect(bar, 6, &r));
  /* DrawMenuBar draws a changed bar: a new item appears at the right of the last one, drawn in COLOR_MENUTEXT */
  CHECK(AppendMenu(bar, MF_STRING, 999, fstr("E&xtra")));
  CHECK(user_menu_item_rect(bar, 6, &r));
  CHECK_EQ(r.left, prev.right);
  CHECK_EQ(count_px(r.left, r.top, r.right, r.bottom, C_MENUTEXT), 0); /* not drawn yet */
  DrawMenuBar(g_top);
  CHECK(count_px(r.left, r.top, r.right, r.bottom, C_MENUTEXT) > 8);
  /* a grayed bar item is drawn in COLOR_GRAYTEXT */
  CHECK_EQ(EnableMenuItem(bar, 999, MF_BYCOMMAND | MF_GRAYED), MF_ENABLED);
  DrawMenuBar(g_top);
  CHECK(count_px(r.left, r.top, r.right, r.bottom, C_GRAYTEXT) > 6);
  CHECK_EQ(count_px(r.left, r.top, r.right, r.bottom, C_MENUTEXT), 0);
  kill_frame();
}

/* Selected item of tracker level k, captured by F-steps. */
static int g_cap_sel[8];
static int g_cap_depth[8];
static int g_ncap;
static void f_capture(void) {
  UMenuTrackInfo ti;
  int k = g_ncap++ & 7;
  g_cap_sel[k] = -99;
  g_cap_depth[k] = -1;
  if (user_menu_track_info(&ti) && ti.active) {
    g_cap_depth[k] = ti.depth;
    g_cap_sel[k] = ti.depth ? ti.lv[ti.depth - 1].sel : -1;
  }
}

static void test_keyboard_tracking(void) {
  HMENU16 bar = load_stars_menu(), file, view, turn, outer, pop;
  unsigned wins;
  make_frame(bar);
  file = GetSubMenu(bar, 0);
  view = GetSubMenu(bar, 1);
  turn = GetSubMenu(bar, 2);
  pop = GetSystemMenu(g_top, 0);
  outer = user_menu_sys_outer(g_top);
  CHECK(pop != 0 && outer != 0);
  wins = user_window_count();

  /* The issue's case: Alt, Right, Down, Enter -> WM_COMMAND with the id of the first item of the
   * second pop-up (Wine select_item 3179-3240, show_sub_popup 3370-3420, exec_focused_item 3472-3512,
   * track_keyboard_menu_bar 4447-4503, defwnd.c 2756-2815). 179 = 0xB3 is "&Toolbar". */
  log_clear();
  s_tap(VK_MENU);
  s_tap(K_RIGHT);
  s_tap(K_DOWN);
  s_tap(VK_RETURN);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MS:%04X/0090/%04X IP:%04X/1/0 MS:00B3/0080/%04X MS:0000/FFFF/0000 CMD:179/0", bar, file, bar,
                 view, bar, view, view));
  CHECK_EQ(user_window_count(), wins); /* the pop-up windows are gone */
  {
    UMenuTrackInfo ti;
    CHECK(!user_menu_track_info(&ti) || !ti.active);
  }

  /* F10 does the same as Alt; the first Esc closes the pop-up (silently, Wine 3282-3310), the second ends the menu.
   * 0x0ED8 = 3800 = "&New..." */
  log_clear();
  s_tap(VK_F10);
  s_tap(K_DOWN);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0ED8/0080/%04X MS:0000/FFFF/0000", bar, file, bar, file, file));
  CHECK_EQ(user_window_count(), wins);

  /* Alt+F: the mnemonic of the bar item selects it and opens its pop-up (Wine 4486-4494) */
  log_clear();
  s_down(VK_MENU);
  s_down('F');
  s_up('F');
  s_up(VK_MENU);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0ED8/0080/%04X MS:0000/FFFF/0000", bar, file, bar, file, file));

  /* Left from the first bar item goes to the control menu (Wine do_next_menu 3678-3790); Right from it returns */
  log_clear();
  s_tap(VK_MENU);
  s_tap(K_LEFT);
  s_tap(K_RIGHT);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MS:%04X/2090/%04X MS:%04X/0090/%04X MS:0000/FFFF/0000", bar, file, bar, pop, outer, file, bar));

  /* Right inside an open pop-up moves to the next bar item and opens its pop-up (Wine menu_right_key 3972-4018) */
  log_clear();
  s_tap(VK_MENU);
  s_tap(K_DOWN);
  s_tap(K_RIGHT);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0ED8/0080/%04X MS:%04X/0090/%04X IP:%04X/1/0 MS:00B3/0080/%04X MS:0000/FFFF/0000",
                 bar, file, bar, file, file, view, bar, view, view));

  /* Enter on a bar item opens its pop-up */
  log_clear();
  s_tap(VK_MENU);
  s_tap(VK_RETURN);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0ED8/0080/%04X MS:0000/FFFF/0000", bar, file, bar, file, file));

  /* Esc on the bar alone ends the menu */
  log_clear();
  s_tap(VK_MENU);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MS:0000/FFFF/0000", bar, file, bar));

  /* Alt pressed again while the menu is up ends it (Wine 4243-4246: VK_MENU / VK_F10 key down) */
  log_clear();
  s_tap(VK_MENU);
  s_down(VK_MENU);
  s_up(VK_MENU);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MS:0000/FFFF/0000", bar, file, bar));

  /* Selection moves skip separators and wrap (Wine move_selection 3253-3277). File = New CustomRace Open
   * Close Save SaveAndSubmit sep PrintMap sep sep Exit (0..10) */
  g_ncap = 0;
  s_tap(VK_MENU);
  s_tap(K_DOWN);                              /* opens File, item 0 */
  s_tap(K_UP);                                /* wraps to the last item, 10 */
  s_fn(f_capture);                            /* 0 */
  s_tap(K_DOWN);                              /* wraps to 0 */
  s_fn(f_capture);                            /* 1 */
  for (int i = 0; i < 6; i++) s_tap(K_DOWN);  /* 1..5, then 7: the separator 6 is skipped */
  s_fn(f_capture);                            /* 2 */
  s_tap(K_DOWN);                              /* 8 and 9 are separators: 10 */
  s_fn(f_capture);                            /* 3 */
  s_tap(K_HOME);
  s_fn(f_capture);                            /* 4 */
  s_tap(K_END);
  s_fn(f_capture);                            /* 5 */
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_EQ(g_cap_depth[0], 2);
  CHECK_EQ(g_cap_sel[0], 10);
  CHECK_EQ(g_cap_sel[1], 0);
  CHECK_EQ(g_cap_sel[2], 7);
  CHECK_EQ(g_cap_sel[3], 10);
  CHECK_EQ(g_cap_sel[4], 0);
  CHECK_EQ(g_cap_sel[5], 10);

  /* A mnemonic inside a pop-up selects and runs the item: "&Close" (113 = 0x71) */
  log_clear();
  s_tap(VK_MENU);
  s_tap(K_DOWN);
  s_tap('C');
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0ED8/0080/%04X MS:0071/0080/%04X MS:0000/FFFF/0000 CMD:113/0", bar, file, bar,
                 file, file, file));

  /* A grayed item does not run: "Turn" has two grayed items (106 = 0x6A "Wait for New", 105 "Generate"); a Right on the
   * bar only moves the selection (Wine menu_right_key: no pop-up was open) */
  log_clear();
  s_tap(VK_MENU);
  s_tap(K_RIGHT);
  s_tap(K_RIGHT);
  s_tap(K_DOWN);
  s_tap(VK_RETURN);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MS:%04X/0090/%04X MS:%04X/0090/%04X IP:%04X/2/0 MS:006A/0081/%04X MS:0000/FFFF/0000", bar,
                 file, bar, view, bar, turn, bar, turn, turn));

  /* A character that matches nothing is offered to the application as WM_MENUCHAR (Wine find_item_by_key
   * 4056-4094) and the menu stays; Win16 packing: wParam = character, lParam = MAKELONG(flags of the menu, menu) */
  log_clear();
  s_tap(VK_MENU);
  s_tap('Z');
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/0090/%04X MC:7A/%lX MS:0000/FFFF/0000", bar, file, bar, (unsigned long)bar << 16));
  kill_frame();
}

static void test_mouse_tracking(void) {
  HMENU16 bar = load_stars_menu(), file, view, cmds, zoom, turn;
  RECT16 r;
  int x, y;
  unsigned wins;
  make_frame(bar);
  file = GetSubMenu(bar, 0);
  view = GetSubMenu(bar, 1);
  turn = GetSubMenu(bar, 2);
  cmds = GetSubMenu(bar, 3);
  zoom = GetSubMenu(view, 3);
  wins = user_window_count();

  /* press on View, drag onto "Find..." (item 2, id 4200 = 0x1068) and release there */
  log_clear();
  CHECK(item_center(bar, 1, &x, &y));
  s_mouse(x, y, 0);
  s_mouse(x, y, RT_BTN_LEFT);
  s_item(view, 2, RT_BTN_LEFT);
  s_item(view, 2, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/1/0 MS:1068/0080/%04X MS:0000/FFFF/0000 CMD:4200/0", bar, view, bar, view, view));
  CHECK_EQ(user_window_count(), wins);

  /* drag across the bar: File, then Commands (its pop-up replaces File's); release over the desktop ends the menu */
  log_clear();
  CHECK(item_center(bar, 0, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_item(bar, 3, RT_BTN_LEFT);
  s_mouse(700, 560, RT_BTN_LEFT);
  s_mouse(700, 560, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:%04X/0090/%04X IP:%04X/3/0 MS:0000/FFFF/0000", bar, file, bar, file, cmds, bar,
                 cmds));

  /* cascade: View -> Zoom -> "100%" (id 3905 = 0xF41) */
  log_clear();
  CHECK(item_center(bar, 1, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_item(view, 3, RT_BTN_LEFT);
  s_item(zoom, 4, RT_BTN_LEFT);
  s_item(zoom, 4, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/1/0 MS:%04X/0090/%04X IP:%04X/3/0 MS:0F41/0080/%04X MS:0000/FFFF/0000 CMD:3905/0", bar, view,
                 bar, view, zoom, view, zoom, zoom));

  /* click and release on the bar item keeps the pop-up open; hovering and clicking a pop-up item then runs it:
   * "Custom Race Wizard" (129 = 0x81) */
  log_clear();
  CHECK(item_center(bar, 0, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_mouse(x, y, 0);
  s_item(file, 1, 0);
  s_item(file, 1, RT_BTN_LEFT);
  s_item(file, 1, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0081/0080/%04X MS:0000/FFFF/0000 CMD:129/0", bar, file, bar, file, file));

  /* a click outside every menu ends the tracking without a command */
  log_clear();
  CHECK(item_center(bar, 0, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_mouse(x, y, 0);
  s_mouse(700, 560, RT_BTN_LEFT);
  s_mouse(700, 560, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0000/FFFF/0000", bar, file, bar, file));

  /* D2, UNVERIFIED choice pinning the CURRENT behaviour: the press that ends the tracking (here on the frame's client area,
   * outside every menu) is consumed by the tracker. Wine's track_menu (menu.c 4144, 4335) leaves it in the queue so the
   * window under the pointer would get WM_LBUTTONDOWN afterwards. Re-queueing is not small here: the message has been
   * through process_mouse (capture routing, double-click memory), so it is recorded in docs/inventory.md section 18 instead. */
  log_clear();
  CHECK(item_center(bar, 0, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_mouse(x, y, 0);
  s_mouse(WX + 400, WY + 250, RT_BTN_LEFT);
  s_mouse(WX + 400, WY + 250, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/0/0 MS:0000/FFFF/0000", bar, file, bar, file));
  CHECK(log_find("T", WM_LBUTTONDOWN, 0) == NULL); /* swallowed; Wine would deliver it (UNVERIFIED divergence) */
  /* control: the same press with no menu open does reach the window, so the log would have shown it */
  s_mouse(WX + 400, WY + 250, 0);
  s_mouse(WX + 400, WY + 250, RT_BTN_LEFT);
  s_mouse(WX + 400, WY + 250, 0);
  run_script();
  CHECK(log_find("T", WM_LBUTTONDOWN, 0) != NULL);

  /* a grayed item does not run on release and the menu stays open (Turn: "Wait for New" = 106 = 0x6A) */
  log_clear();
  CHECK(item_center(bar, 2, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_item(turn, 0, RT_BTN_LEFT);
  s_item(turn, 0, 0);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F090 IM:%04X/0 MS:%04X/0090/%04X IP:%04X/2/0 MS:006A/0081/%04X MS:0000/FFFF/0000", bar, turn, bar, turn, turn));

  CHECK(!user_menu_item_rect(file, 0, &r)); /* nothing on screen any more */
  CHECK_EQ(user_window_count(), wins);
  kill_frame();
}

/* ---- TrackPopupMenu ---- */

static uint16_t track(HMENU16 m, uint16_t flags, int x, int y) {
  uint16_t r;
  g_iscript = 0;
  user_wait_hook = feeder;
  r = TrackPopupMenu(m, flags, (int16_t)x, (int16_t)y, 0, g_top, 0);
  user_wait_hook = NULL;
  user_input_exhausted = 0;
  s_clear();
  return r;
}

static RECT16 g_prect;
static RECT16 g_irect[8];
static int g_pok;
static void f_probe(void) {
  UMenuTrackInfo ti;
  g_pok = 0;
  if (user_menu_track_info(&ti) && ti.active && ti.depth > 0) {
    g_prect = ti.lv[0].rect;
    g_pok = 1;
    for (unsigned i = 0; i < 8; i++)
      if (!user_menu_item_rect(ti.lv[0].menu, i, &g_irect[i])) memset(&g_irect[i], 0, sizeof g_irect[i]);
  }
}

static void drain_messages(void) {
  FARPTR p = far_alloc(sizeof(MSG16));
  int guard = 0;
  while (guard++ < 200 && PeekMessage(p, 0, 0, 0, PM_REMOVE)) {
    TranslateMessage(p);
    DispatchMessage(p);
  }
}

static void test_track_popup_menu(void) {
  HMENU16 m = CreatePopupMenu(), sub = CreatePopupMenu();
  uint16_t r;
  FARPTR p = far_alloc(sizeof(MSG16));
  int x, y;
  unsigned wins;
  CHECK(AppendMenu(m, MF_STRING, 100, fstr("One")));
  CHECK(AppendMenu(m, MF_STRING, 101, fstr("Two")));
  CHECK(AppendMenu(m, MF_SEPARATOR, 0, 0));
  CHECK(AppendMenu(m, MF_STRING | MF_GRAYED, 102, fstr("Three")));
  CHECK(AppendMenu(m, MF_STRING | MF_CHECKED, 103, fstr("Four\tCtrl+4")));
  CHECK(AppendMenu(m, MF_POPUP, sub, fstr("More")));
  CHECK(AppendMenu(sub, MF_STRING, 200, fstr("Deep")));
  make_frame(0);
  wins = user_window_count();

  /* keys: Down Down Enter with TPM_RETURNCMD returns the id and posts no command (Wine 4540-4560) */
  log_clear();
  s_tap(K_DOWN);
  s_tap(K_DOWN);
  s_tap(VK_RETURN);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 101);
  CHECK_TRACE(fm("IM:%04X/0 IP:%04X/0/0 MS:0064/0080/%04X MS:0065/0080/%04X MS:0000/FFFF/0000", m, m, m, m));
  CHECK_EQ(PeekMessage(p, g_top, WM_COMMAND, WM_COMMAND, PM_REMOVE), 0);
  CHECK_EQ(user_window_count(), wins);

  /* the game's way (flags 4 = TPM_CENTERALIGN): no TPM_RETURNCMD, the command is posted and PeekMessage(WM_COMMAND) finds it.
   * The game's wRemoveMsg is 2 = PM_NOYIELD (bytes 6A 02 at seg04 0x4904), which does NOT remove: the command stays queued. */
  s_tap(K_DOWN);
  s_tap(VK_RETURN);
  r = track(m, TPM_LEFTBUTTON | TPM_CENTERALIGN, 200, 100);
  CHECK_EQ(r, 1);
  CHECK_EQ(PeekMessage(p, g_top, WM_COMMAND, WM_COMMAND, PM_NOYIELD), 1);
  CHECK_EQ(((MSG16 *)LIN(p))->wParam, 100);
  CHECK_EQ(((MSG16 *)LIN(p))->hwnd, g_top);
  CHECK_EQ(((MSG16 *)LIN(p))->lParam, 0);
  /* still queued: a second peek finds it again, and it is delivered later by the application's loop */
  CHECK_EQ(PeekMessage(p, g_top, WM_COMMAND, WM_COMMAND, PM_NOYIELD), 1);
  CHECK_EQ(((MSG16 *)LIN(p))->wParam, 100);
  log_clear();
  drain_messages();
  CHECK_TRACE("CMD:100/0");
  CHECK_EQ(PeekMessage(p, g_top, WM_COMMAND, WM_COMMAND, PM_NOYIELD), 0);

  /* a grayed item does not run (Down x3 skips the separator onto Three); Esc cancels: 0 */
  s_tap(K_DOWN);
  s_tap(K_DOWN);
  s_tap(K_DOWN);
  s_tap(VK_RETURN);
  s_tap(VK_ESCAPE);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 0);
  CHECK_EQ(PeekMessage(p, g_top, WM_COMMAND, WM_COMMAND, PM_REMOVE), 0);

  /* placement (M5) and size (M3): 5 items of 18 and a separator of 9, plus the border: 101 */
  {
    static const struct {
      uint16_t flags;
      int x, y;
    } T[5] = {{TPM_LEFTALIGN, 200, 100}, {TPM_CENTERALIGN, 300, 100}, {TPM_RIGHTALIGN, 400, 100}, {TPM_LEFTALIGN, GDI_SCREEN_W - 6, 100},
              {TPM_LEFTALIGN, 200, GDI_SCREEN_H - 6}};
    int w = 0, h = 0;
    CHECK(user_menu_popup_size(m, &w, &h));
    CHECK_EQ(h, 101);
    CHECK(w > 60);
    for (int i = 0; i < 5; i++) {
      s_fn(f_probe);
      s_tap(VK_ESCAPE);
      g_pok = 0;
      r = track(m, (uint16_t)(T[i].flags | TPM_RETURNCMD), T[i].x, T[i].y);
      CHECK_EQ(r, 0);
      CHECK(g_pok);
      CHECK_EQ(g_prect.right - g_prect.left, w);
      CHECK_EQ(g_prect.bottom - g_prect.top, h);
      switch (i) {
        case 0:
          CHECK_EQ(g_prect.left, 200);
          CHECK_EQ(g_prect.top, 100);
          break;
        case 1: CHECK_EQ(g_prect.left, 300 - w / 2); break;
        case 2: CHECK_EQ(g_prect.right, 400); break;
        case 3:
          CHECK(g_prect.right <= GDI_SCREEN_W);
          CHECK(g_prect.left >= 0 && g_prect.left < GDI_SCREEN_W - 6);
          break;
        default:
          CHECK(g_prect.bottom <= GDI_SCREEN_H);
          CHECK(g_prect.top >= 0 && g_prect.top < GDI_SCREEN_H - 6);
          break;
      }
    }
    /* item rectangles: border 1, items 18 high, the separator 9 */
    s_fn(f_probe);
    s_tap(VK_ESCAPE);
    track(m, TPM_RETURNCMD, 200, 100);
    CHECK(g_pok);
    CHECK_EQ(g_irect[0].top, 100 + 1);
    CHECK_EQ(g_irect[0].bottom - g_irect[0].top, 18);
    CHECK_EQ(g_irect[2].bottom - g_irect[2].top, 9);
    CHECK_EQ(g_irect[3].top, g_irect[2].bottom);
    CHECK_EQ(g_irect[5].bottom, 100 + 101 - 1);
    CHECK_EQ(g_irect[0].left, g_prect.left + 1);
    CHECK_EQ(g_irect[0].right, g_prect.right - 1);
    CHECK(g_irect[6].right == 0);
  }

  /* sub-menu: End selects "More", Right opens its pop-up with the first item selected, Left closes it, Esc ends */
  log_clear();
  s_tap(K_END);
  s_tap(K_RIGHT);
  s_tap(K_LEFT);
  s_tap(VK_ESCAPE);
  r = track(m, TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 0);
  CHECK_TRACE(fm("IM:%04X/0 IP:%04X/0/0 MS:%04X/0090/%04X IP:%04X/5/0 MS:00C8/0080/%04X MS:0000/FFFF/0000", m, m, sub, m, sub, sub));

  /* mouse: right button (TPM_RIGHTBUTTON), left button, hover then Enter, click outside */
  s_item(m, 1, RT_BTN_RIGHT);
  s_item(m, 1, 0);
  r = track(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 101);
  s_item(m, 1, RT_BTN_RIGHT);
  s_item(m, 1, 0);
  s_tap(VK_ESCAPE);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100); /* a right click does nothing without TPM_RIGHTBUTTON */
  CHECK_EQ(r, 0);
  s_item(m, 0, RT_BTN_LEFT);
  s_item(m, 0, 0);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 100);
  s_item(m, 1, 0);
  s_tap(VK_RETURN);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 101);
  s_mouse(700, 560, RT_BTN_LEFT);
  s_mouse(700, 560, 0);
  r = track(m, TPM_LEFTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 0);
  /* the right button released outside does not end a pop-up menu (M9), Esc does */
  s_mouse(700, 560, RT_BTN_RIGHT);
  s_mouse(700, 560, 0);
  s_tap(VK_ESCAPE);
  r = track(m, TPM_RIGHTBUTTON | TPM_RETURNCMD, 200, 100);
  CHECK_EQ(r, 0);
  CHECK(!item_center(m, 0, &x, &y));

  CHECK_EQ(TrackPopupMenu(0x1FFF, 0, 10, 10, 0, g_top, 0), 0);
  CHECK_EQ(TrackPopupMenu(m, 0, 10, 10, 0, 0x7777, 0), 0);
  CHECK_EQ(user_window_count(), wins);
  kill_frame();
  CHECK(DestroyMenu(m));
}

/* ---- the system menu ---- */

static UMenuTrackInfo g_snap;
static int g_snap_ok;
static void f_snap(void) { g_snap_ok = user_menu_track_info(&g_snap) ? 1 : 0; }

static void test_system_menu(void) {
  HMENU16 bar = load_stars_menu(), pop, outer;
  unsigned wins;
  UMenuItemInfo it;
  USysItem si[10];
  int bx = WX + 10, by = WY + 10; /* inside the control-menu box (window coordinates 10,10) */
  make_frame(bar);
  pop = GetSystemMenu(g_top, 0);
  outer = user_menu_sys_outer(g_top);
  CHECK(pop != 0 && outer != 0 && pop != outer);
  CHECK_EQ(GetMenuItemCount(pop), 9);
  CHECK_EQ(user_sysmenu_items(g_top, si), 9);
  for (unsigned i = 0; i < 9; i++) {
    CHECK(user_menu_item_info(pop, i, &it));
    if (si[i].id == 0) {
      CHECK(it.flags & MF_SEPARATOR);
    } else {
      CHECK_EQ(it.id, si[i].id);
      CHECK(strcmp(it.text, si[i].text) == 0);
    }
  }
  CHECK(user_menu_item_info(pop, 6, &it));
  CHECK_EQ(it.id, SC_CLOSE);
  CHECK(strcmp(it.text, "&Close\tAlt+F4") == 0);
  /* a window without WS_SYSMENU has none */
  {
    HWND16 q = mk("Q", CLS, "x", WS_POPUP | WS_VISIBLE, 300, 100, 100, 100, 0, 0);
    CHECK(q != 0);
    CHECK_EQ(GetSystemMenu(q, 0), 0);
    DestroyWindow(q);
  }
  wins = user_window_count();

  /* a click on the box opens the pop-up under it (issue dependency 1; Wine defwnd.c 2223, menu.c 3555-3572, 4428-4445):
   * WM_SYSCOMMAND SC_MOUSEMENU + HTSYSMENU (0xF093), WM_INITMENU with the outer menu, the item is announced with MF_SYSMENU,
   * WM_INITMENUPOPUP carries the system-menu flag; Close (SC_CLOSE = 0xF060) is posted as WM_SYSCOMMAND */
  log_clear();
  s_mouse(bx, by, 0);
  s_mouse(bx, by, RT_BTN_LEFT);
  s_mouse(bx, by, 0);
  s_fn(f_snap);
  s_item(pop, 6, 0);
  s_item(pop, 6, RT_BTN_LEFT);
  s_item(pop, 6, 0);
  g_snap_ok = 0;
  run_script();
  CHECK(g_snap_ok && g_snap.active);
  CHECK_EQ(g_snap.depth, 2);
  CHECK_EQ(g_snap.lv[0].menu, outer);
  CHECK_EQ(g_snap.lv[1].menu, pop);
  CHECK_EQ(g_snap.lv[1].rect.left, WX + 4);  /* under the box, left edges aligned (UNVERIFIED) */
  CHECK_EQ(g_snap.lv[1].rect.top, WY + 4 + 19);
  CHECK_TRACE(fm("SYS:F093 IM:%04X/0 MS:%04X/2090/%04X IP:%04X/0/1 MS:F060/2080/%04X MS:0000/FFFF/0000 SYS:F060", outer, pop, outer, pop, pop));
  CHECK(!IsWindow(g_top)); /* DefWindowProc turned SC_CLOSE into WM_CLOSE and DestroyWindow */
  g_top = 0;
  CHECK_EQ(user_window_count(), 0);
  (void)wins;

  /* Alt+Space opens it from the keyboard: the pop-up opens with its first item selected (Restore, grayed here) */
  bar = load_stars_menu();
  make_frame(bar);
  pop = GetSystemMenu(g_top, 0);
  outer = user_menu_sys_outer(g_top);
  log_clear();
  s_down(VK_MENU);
  s_down(VK_SPACE);
  s_up(VK_SPACE);
  s_up(VK_MENU);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F100 IM:%04X/0 MS:%04X/2090/%04X IP:%04X/0/1 MS:F120/2081/%04X MS:0000/FFFF/0000", outer, pop, outer, pop, pop));
  CHECK(IsWindow(g_top));

  /* a grayed item (Restore) does not run; Move (enabled) is posted as SC_MOVE */
  log_clear();
  s_mouse(bx, by, RT_BTN_LEFT);
  s_mouse(bx, by, 0);
  s_item(pop, 0, RT_BTN_LEFT);
  s_item(pop, 0, 0);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK_TRACE(fm("SYS:F093 IM:%04X/0 MS:%04X/2090/%04X IP:%04X/0/1 MS:F120/2081/%04X MS:0000/FFFF/0000", outer, pop, outer, pop, pop));
  CHECK_EQ(user_window_count(), 1);

  /* Move (item 1, enabled) is posted as SC_MOVE (0xF010) when released over */
  log_clear();
  s_mouse(bx, by, RT_BTN_LEFT);
  s_mouse(bx, by, 0);
  s_item(pop, 1, RT_BTN_LEFT);
  s_item(pop, 1, 0);
  run_script();
  CHECK_TRACE(fm("SYS:F093 IM:%04X/0 MS:%04X/2090/%04X IP:%04X/0/1 MS:F010/2080/%04X MS:0000/FFFF/0000 SYS:F010", outer, pop, outer, pop, pop));

  /* a double click on the box closes the window instead of opening the pop-up (M7); the hook still wins when set */
  log_clear();
  g_dt = 40;
  s_mouse(bx, by, RT_BTN_LEFT);
  s_mouse(bx, by, 0);
  s_mouse(bx, by, RT_BTN_LEFT);
  s_mouse(bx, by, 0);
  run_script();
  g_dt = 600;
  CHECK(!IsWindow(g_top));
  g_top = 0;
  kill_frame();
}

/* ---- accelerators ---- */

static void press_mod(unsigned vk, int down) {
  FARPTR p = far_alloc(sizeof(MSG16));
  rt_input_key((uint16_t)vk, 0x1D, down, g_t += 10);
  while (PeekMessage(p, 0, 0, 0, PM_REMOVE)) DispatchMessage(p);
}

static FARPTR key_msg(uint16_t msg, uint16_t vk) {
  FARPTR p = far_alloc(sizeof(MSG16));
  MSG16 *m = LIN(p);
  memset(m, 0, sizeof *m);
  m->hwnd = g_top;
  m->message = msg;
  m->wParam = vk;
  m->lParam = 1;
  return p;
}

/* D1: Alt+F4 reaches DefWindowProc of whichever window had the focus; Wine (defwnd.c 2766-2771) POSTS SC_CLOSE to the
 * top-level (root) window and skips classes with CS_NOCLOSE (0x0200). The focused child must survive. */
static void test_alt_f4(void) {
  HWND16 top, btn, st, top2;
  top = mk("F4", CLS, "F4 top", TOPW, 50, 50, 300, 200, 0, 0);
  btn = mk("F4b", "BUTTON", "b", WS_CHILD | WS_VISIBLE, 5, 5, 60, 20, top, 71);
  st = mk("F4s", "STATIC", "s", WS_CHILD | WS_VISIBLE, 5, 40, 60, 20, top, 72);
  CHECK(top && btn && st);
  drain_messages();
  SendMessage(btn, WM_SYSKEYDOWN, 0x73, 0x20000001UL);
  SendMessage(st, WM_SYSKEYDOWN, 0x73, 0x20000001UL);
  CHECK(IsWindow(btn));
  CHECK(IsWindow(st));
  CHECK(IsWindow(top)); /* posted, not sent: nothing is closed until the message is processed */
  drain_messages();
  CHECK(!IsWindow(top));
  CHECK(!IsWindow(btn)); /* children go with the top-level window */
  CHECK(!IsWindow(st));

  /* a child alone must not be the one closed: the top-level window gets WM_CLOSE */
  top = mk("F4", CLS, "F4 top", TOPW, 50, 50, 300, 200, 0, 0);
  btn = mk("F4b", "BUTTON", "b", WS_CHILD | WS_VISIBLE, 5, 5, 60, 20, top, 71);
  log_clear();
  SendMessage(btn, WM_SYSKEYDOWN, 0x73, 0x20000001UL);
  drain_messages();
  CHECK(log_find("F4", WM_CLOSE, 0) != NULL);
  CHECK(log_find("F4b", WM_CLOSE, 0) == NULL);

  /* CS_NOCLOSE: nothing happens */
  CHECK(register_class("NOCLOSECLS", FP_PROC_A, 0x0200, 0) != 0);
  top2 = mk("F4n", "NOCLOSECLS", "no close", TOPW, 60, 60, 200, 150, 0, 0);
  btn = mk("F4nb", "BUTTON", "b", WS_CHILD | WS_VISIBLE, 5, 5, 60, 20, top2, 73);
  CHECK(top2 && btn);
  drain_messages();
  SendMessage(top2, WM_SYSKEYDOWN, 0x73, 0x20000001UL);
  SendMessage(btn, WM_SYSKEYDOWN, 0x73, 0x20000001UL);
  drain_messages();
  CHECK(IsWindow(top2));
  CHECK(IsWindow(btn));
  DestroyWindow(top2);
}

/* F10 is accelerator 96 in the game's table 116: the application loop translates it, so DefWindowProc never sees
 * WM_SYSKEYDOWN F10 and the menu bar does not open (UNVERIFIED for Windows 3.1; docs/inventory.md section 18). A grayed
 * pop-up item of the bar does not open its pop-up. */
static void test_f10_and_grayed_bar(void) {
  HMENU16 bar = load_stars_menu(), gbar = CreateMenu(), sub = CreatePopupMenu(), sub2 = CreatePopupMenu();
  HACCEL16 a116 = LoadAccelerators(g_hinst, MK_FP(0, 116));
  unsigned wins;
  int x, y;
  CHECK(a116 != 0);
  make_frame(bar);
  wins = user_window_count();
  g_accel_loop = a116;
  log_clear();
  s_down(VK_F10);
  s_up(VK_F10);
  run_script();
  CHECK_TRACE("CMD:96/10000");
  CHECK_EQ(user_window_count(), wins);
  kill_frame();

  CHECK(AppendMenu(sub, MF_STRING, 300, fstr("Sub")));
  CHECK(AppendMenu(sub2, MF_STRING, 301, fstr("Sub2")));
  CHECK(AppendMenu(gbar, MF_POPUP | MF_GRAYED, sub, fstr("Gray")));
  CHECK(AppendMenu(gbar, MF_POPUP, sub2, fstr("Live")));
  make_frame(gbar);
  wins = user_window_count();
  CHECK(item_center(gbar, 0, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_mouse(x, y, 0);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK(strstr(trace(), "IP:") == NULL); /* WM_INITMENUPOPUP never sent for the grayed item */
  CHECK_EQ(user_window_count(), wins);   /* and no pop-up window appeared */
  log_clear();
  CHECK(item_center(gbar, 1, &x, &y));
  s_mouse(x, y, RT_BTN_LEFT);
  s_mouse(x, y, 0);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();
  CHECK(strstr(trace(), "IP:") != NULL); /* control: the enabled item does open */
  kill_frame();
}

static void test_translate_accelerator(void) {
  HMENU16 bar = CreateMenu(), popup = CreatePopupMenu();
  HACCEL16 a116 = LoadAccelerators(g_hinst, MK_FP(0, 116)), a1080 = LoadAccelerators(g_hinst, MK_FP(0, 1080));
  CHECK(AppendMenu(popup, MF_STRING, 138, fstr("F1 item")));
  CHECK(AppendMenu(popup, MF_STRING | MF_GRAYED, 136, fstr("F2 item, grayed")));
  CHECK(AppendMenu(bar, MF_POPUP, popup, fstr("&Menu")));
  make_frame(bar);

  /* Ctrl+A is the first entry of table 116: WM_COMMAND 1064, HIWORD(lParam) = 1 (issue test case; Wine translate_accelerator 1713-1872) */
  log_clear();
  press_mod(VK_CONTROL, 1);
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x41)), 1);
  CHECK_TRACE("CMD:1064/10000");
  press_mod(VK_CONTROL, 0);

  /* without Ctrl, or with an extra Shift, the entry does not match; a key up never translates */
  log_clear();
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x41)), 0);
  press_mod(VK_CONTROL, 1);
  press_mod(VK_SHIFT, 1);
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x41)), 0);
  press_mod(VK_SHIFT, 0);
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYUP, 0x41)), 0);
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x42)), 0); /* 'B' is in no entry */
  press_mod(VK_CONTROL, 0);
  CHECK_TRACE("");

  /* F1 alone is entry 6 (id 138). The command is in the window's menu: Wine sends WM_INITMENU and WM_INITMENUPOPUP first */
  log_clear();
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x70)), 1);
  CHECK_TRACE(fm("IM:%04X/0 IP:%04X/0/0 CMD:138/10000", bar, popup));

  /* F2 (id 136) is grayed in the menu: the message is eaten (returns 1), no command is sent */
  log_clear();
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x71)), 1);
  CHECK_TRACE(fm("IM:%04X/0 IP:%04X/0/0", bar, popup));

  /* a disabled window gets nothing */
  EnableWindow(g_top, 0);
  log_clear();
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_KEYDOWN, 0x70)), 1);
  CHECK_TRACE("");
  EnableWindow(g_top, 1);

  /* table 1080: Alt+N (flags 0x13 = FVIRTKEY | FNOINVERT | FALT) sends 250 */
  log_clear();
  press_mod(VK_MENU, 1);
  CHECK_EQ(TranslateAccelerator(g_top, a1080, key_msg(WM_SYSKEYDOWN, 0x4E)), 1);
  press_mod(0x4E, 1); /* the key itself, so that releasing Alt is not an Alt tap */
  press_mod(0x4E, 0);
  press_mod(VK_MENU, 0);
  CHECK_TRACE("CMD:250/10000");

  /* a bad handle, a bad window and a message that is no key message */
  CHECK_EQ(TranslateAccelerator(g_top, 0x1FFF, key_msg(WM_KEYDOWN, 0x41)), 0);
  CHECK_EQ(TranslateAccelerator(0, a116, key_msg(WM_KEYDOWN, 0x70)), 0);
  CHECK_EQ(TranslateAccelerator(g_top, a116, key_msg(WM_PAINT, 0x70)), 0);

  /* through the application loop, as a game runs it: Ctrl+A typed, WM_COMMAND arrives, the key is not dispatched */
  g_accel_loop = a116;
  log_clear();
  s_down(VK_CONTROL);
  s_down('A');
  s_up('A');
  s_up(VK_CONTROL);
  run_script();
  g_accel_loop = 0;
  {
    int ncmd = 0;
    for (int i = 0; i < g_nlog; i++)
      if (g_log[i].msg == WM_COMMAND) ncmd++;
    CHECK_EQ(ncmd, 1);
    CHECK(log_find("T", WM_CHAR, 0) == NULL); /* the translated key never reached TranslateMessage */
  }
  kill_frame();
}

/* ---- pixels (all UNVERIFIED, see the file comment: M1-M4) ---- */

static RECT16 g_b0, g_b1;
static HMENU16 g_pbar;

static void f_pixels_bar(void) {
  /* Alt was tapped: "File" is highlighted, "View" is not */
  CHECK_EQ(PXL(g_b0.left + 1, g_b0.top + 1), C_HILITE);
  CHECK(count_px(g_b0.left, g_b0.top, g_b0.right, g_b0.bottom, C_HILITETEXT) > 8);
  CHECK_EQ(count_px(g_b1.left, g_b1.top, g_b1.right, g_b1.bottom, C_HILITE), 0);
  CHECK_EQ(PXL(g_b1.left + 1, g_b1.top + 1), C_MENU);
  dump_frame("menu_bar_active", WX, WY, 300, 60);
}

static void f_pixels_view_popup(void) {
  UMenuTrackInfo ti;
  RECT16 pr, i0, i1, i2;
  HMENU16 view = GetSubMenu(g_pbar, 1);
  CHECK(user_menu_track_info(&ti) && ti.active && ti.depth == 2);
  pr = ti.lv[1].rect;
  CHECK_EQ(ti.lv[1].menu, view);
  CHECK(user_menu_item_rect(view, 0, &i0) && user_menu_item_rect(view, 1, &i1) && user_menu_item_rect(view, 2, &i2));
  /* popup top-left under the bar item, as Wine's show_sub_popup places it (3398-3417) */
  CHECK_EQ(pr.left, g_b1.left);
  CHECK_EQ(pr.top, g_b1.bottom); /* the pop-up's top border is the row of the bar's bottom line */
  /* M3: a 1-pixel COLOR_WINDOWFRAME border */
  CHECK_EQ(PXL(pr.left, pr.top + 20), C_FRAME);
  CHECK_EQ(PXL(pr.right - 1, pr.top + 20), C_FRAME);
  CHECK_EQ(PXL(pr.left + 30, pr.top), C_FRAME);
  CHECK_EQ(PXL(pr.left + 30, pr.bottom - 1), C_FRAME);
  /* M4: the selected first item (Toolbar) is highlighted, the others are on COLOR_MENU */
  CHECK_EQ(PXL(i0.left + 1, i0.top + 1), C_HILITE);
  CHECK_EQ(PXL(i0.right - 2, i0.bottom - 2), C_HILITE);
  CHECK(count_px(i0.left, i0.top, i0.right, i0.bottom, C_HILITETEXT) > 8);
  CHECK_EQ(PXL(i2.left + 1, i2.top + 1), C_MENU);
  CHECK(count_px(i2.left, i2.top, i2.right, i2.bottom, C_MENUTEXT) > 10);
  /* the separator: a COLOR_WINDOWFRAME line across the middle, COLOR_MENU above it */
  CHECK_EQ(PXL(i1.left + 10, (i1.top + i1.bottom) / 2), C_FRAME);
  CHECK_EQ(PXL(i1.left + 10, i1.top + 1), C_MENU);
  CHECK_EQ(PXL(i1.right - 10, (i1.top + i1.bottom) / 2), C_FRAME);
  dump_frame("menu_popup_view", pr.left - 4, pr.top - 30, 260, 240);
}

static void f_pixels_file_popup(void) {
  UMenuTrackInfo ti;
  RECT16 i2, i3;
  HMENU16 file = GetSubMenu(g_pbar, 0);
  CHECK(user_menu_track_info(&ti) && ti.active && ti.depth == 2);
  CHECK(user_menu_item_rect(file, 2, &i2) && user_menu_item_rect(file, 3, &i3));
  /* "Open...<TAB>Ctrl+O": the part after the tab is drawn in a second column at the right end */
  CHECK(count_px(i2.right - 50, i2.top, i2.right - 4, i2.bottom, C_MENUTEXT) > 6);
  /* "Close" has no accelerator text: its right end is empty */
  CHECK_EQ(count_px(i3.right - 50, i3.top, i3.right - 4, i3.bottom, C_MENUTEXT), 0);
}

static void f_pixels_turn_popup(void) {
  RECT16 i1;
  HMENU16 turn = GetSubMenu(g_pbar, 2);
  CHECK(user_menu_item_rect(turn, 1, &i1));
  /* "Generate" is grayed and not selected: COLOR_GRAYTEXT text, no COLOR_MENUTEXT pixel */
  CHECK(count_px(i1.left, i1.top, i1.right, i1.bottom, C_GRAYTEXT) > 6);
  CHECK_EQ(count_px(i1.left, i1.top, i1.right, i1.bottom, C_MENUTEXT), 0);
}

static void f_pixels_zoom_popup(void) {
  UMenuTrackInfo ti;
  RECT16 i3, i4;
  HMENU16 zoom = GetSubMenu(GetSubMenu(g_pbar, 1), 3);
  CHECK(user_menu_track_info(&ti) && ti.active && ti.depth == 3);
  CHECK_EQ(ti.lv[2].menu, zoom);
  CHECK(user_menu_item_rect(zoom, 3, &i3) && user_menu_item_rect(zoom, 4, &i4));
  /* sub-menu to the right of its parent item, overlapping the parent's border by one pixel (Wine 3398-3410) */
  CHECK_EQ(ti.lv[2].rect.left, ti.lv[1].rect.right - 1);
  /* "100%" is checked: a check mark in the 16-pixel column at the left; "75%" has none (M3) */
  CHECK(count_px(i4.left, i4.top, i4.left + 16, i4.bottom, C_MENUTEXT) > 3);
  CHECK_EQ(count_px(i3.left, i3.top, i3.left + 16, i3.bottom, C_MENUTEXT), 0);
  dump_frame("menu_popup_zoom", ti.lv[1].rect.left - 4, ti.lv[1].rect.top - 30, 360, 240);
}

static void test_pixels(void) {
  HMENU16 bar = load_stars_menu();
  RECT16 b5;
  int mx, my;
  make_frame(bar);
  g_pbar = bar;
  CHECK(user_menu_item_rect(bar, 0, &g_b0));
  CHECK(user_menu_item_rect(bar, 1, &g_b1));
  CHECK(user_menu_item_rect(bar, 5, &b5));
  UpdateWindow(g_top);
  /* idle bar (M1, M4): COLOR_MENU to the right of the last item, text in COLOR_MENUTEXT, a black line below the items */
  mx = b5.right + 20;
  my = WY + 24 + 5;
  CHECK_EQ(PXL(mx, my), C_MENU);
  CHECK_EQ(PXL(mx, WY + 24 + 18), C_FRAME);
  CHECK_EQ(PXL(WX + 4, WY + 24 + 18), C_FRAME);
  CHECK_EQ(PXL(WX + WW - 5, WY + 24 + 18), C_FRAME);
  CHECK(count_px(g_b0.left, g_b0.top, g_b0.right, g_b0.bottom, C_MENUTEXT) > 12);
  CHECK_EQ(count_px(g_b0.left, g_b0.top, g_b0.right, g_b0.bottom, C_HILITE), 0);
  CHECK_EQ(PXL(WX + 4, WY + 24 - 1), C_FRAME); /* the separator under the caption (user_nc.c) */
  dump_frame("menu_bar", WX, WY, 300, 60);

  s_tap(VK_MENU);
  s_fn(f_pixels_bar);
  s_tap(K_RIGHT);
  s_tap(K_DOWN);
  s_fn(f_pixels_view_popup);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();

  s_tap(VK_MENU);
  s_tap(K_DOWN);
  s_fn(f_pixels_file_popup);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();

  s_tap(VK_MENU);
  s_tap(K_RIGHT);
  s_tap(K_RIGHT);
  s_tap(K_DOWN);
  s_fn(f_pixels_turn_popup);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();

  CHECK_EQ(CheckMenuItem(bar, 3905, MF_BYCOMMAND | MF_CHECKED), MF_UNCHECKED);
  s_tap(VK_MENU);
  s_tap(K_RIGHT);
  s_tap(K_DOWN);        /* View: Toolbar */
  s_tap(K_DOWN);        /* Find... (the separator is skipped) */
  s_tap(K_DOWN);        /* Zoom */
  s_tap(K_RIGHT);       /* opens Zoom, first item selected */
  s_fn(f_pixels_zoom_popup);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  s_tap(VK_ESCAPE);
  run_script();

  /* after the menu closed the pop-up area is repainted by the window system (the client area shows again) */
  {
    UMenuTrackInfo ti;
    CHECK(!user_menu_track_info(&ti) || !ti.active);
  }
  kill_frame();
}

int main(void) {
  KInitTaskRegs regs;
  FILE *fp;
  long len;
  (void)dump;
  if (fixture_open() != 0) return 1;
  fp = fopen(EXE, "rb");
  if (!fp) return 1;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  g_img = malloc((size_t)len);
  if (!g_img || fread(g_img, 1, (size_t)len, fp) != (size_t)len) return 1;
  fclose(fp);
  CHECK_EQ(kernel_load(g_img, (size_t)len), 0);
  InitTask(&regs);
  g_hinst = regs.di;
  CHECK(g_hinst != 0);
  CHECK(register_class(CLS, FP_PROC_A, 0, 0) != 0);
  test_constants();
  test_starsmenu_resource();
  test_accel_tables();
  test_menu_api();
  test_menu_in_window();
  test_class_menu();
  test_bar_layout();
  test_keyboard_tracking();
  test_mouse_tracking();
  test_track_popup_menu();
  test_system_menu();
  test_alt_f4();
  test_f10_and_grayed_bar();
  test_translate_accelerator();
  test_pixels();
  kernel_shutdown();
  fixture_close();
  free(g_img);
  return TEST_DONE();
}
