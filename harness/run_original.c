/* run_original: runs the ORIGINAL stars26i.exe on the runtime under the Unicorn harness
 * (milestone M3, docs/PLAN.md section 5), headless and scripted.
 *
 *   build/run_original [--strict] [--script FILE] [--dump DIR] [--exe FILE]
 *                      [--max-api N] [--timeout SECONDS] [--list-imports]
 *                      [--put HOSTFILE GUESTPATH]... [--unregister MODULE.ORDINAL]...
 *
 * The game runs from its entry point (segment 34 : 0x001A). Input comes from the script, which is
 * consumed one line at a time whenever the game's GetMessage finds nothing to do (user_wait_hook),
 * so every `dump` shows a fully painted idle state. The run ends at WM_QUIT or when the script
 * is exhausted (GetMessage then reports WM_QUIT). Script lines (# starts a comment):
 *   wait <ms>        advance the runtime clock (one-second resolution) by ms, rounded up
 *   click <x> <y>    move to screen (x, y), press and release the left button
 *   rclick <x> <y>   same with the right button
 *   key <vk>         press and release a key; vk is a number (0x.. allowed), a VK_ name, or a chord
 *                    such as VK_MENU+F (modifiers held while the last key is pressed); a single
 *                    letter or digit stands for its key (F is 0x46)
 *   type <text>      press and release the keys that type <text> (a-z A-Z 0-9 space , . - / ; ')
 *                    type $NAME types the value of environment variable NAME instead (secrets stay
 *                    out of the scripts and the logs)
 *   dump <name>      write <dir>/<name>.bmp (24-bit, the 800x600 screen)
 * Exit status: 0 when the run ended normally (WM_QUIT or script end), 1 on an emulation fault or
 * bad usage, 2 on a timeout or API limit, 3 when strict mode aborted on an unimplemented API.
 * Native only; never part of the wasm build. */
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness/commdlg_thunks.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/user.h"
#include "runtime/vfs.h"

#define MAX_LINES 4096

static char *g_lines[MAX_LINES];
static int g_nlines, g_next;
static const char *g_dump_dir = "re/frames";
static int g_dumps_written;
static int g_run_started;

static void report_state(const char *why);
static void print_log(void);

/* ---- BMP dump ---- */

static int ensure_dir(const char *path) {
  char buf[1024];
  size_t n = strlen(path);
  if (n >= sizeof buf) return -1;
  memcpy(buf, path, n + 1);
  for (size_t i = 1; i <= n; i++) {
    if (buf[i] == '/' || buf[i] == 0) {
      char c = buf[i];
      buf[i] = 0;
      if (mkdir(buf, 0777) != 0 && errno != EEXIST) return -1;
      buf[i] = c;
    }
  }
  return 0;
}

static void le32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static int dump_frame(const char *name) {
  int w = fb_width(), h = fb_height();
  size_t npx = (size_t)w * (size_t)h;
  uint32_t *rgba = malloc(npx * 4);
  uint8_t hdr[54];
  uint32_t row = ((uint32_t)w * 3 + 3) & ~3u;
  uint8_t *line = calloc(1, row);
  char path[1200];
  FILE *fp;
  if (!rgba || !line) { free(rgba); free(line); return -1; }
  if (ensure_dir(g_dump_dir) != 0) {
    fprintf(stderr, "run_original: cannot create %s\n", g_dump_dir);
    free(rgba); free(line);
    return -1;
  }
  snprintf(path, sizeof path, "%s/%s.bmp", g_dump_dir, name);
  fp = fopen(path, "wb");
  if (!fp) { fprintf(stderr, "run_original: cannot write %s\n", path); free(rgba); free(line); return -1; }
  fb_present(rgba);
  memset(hdr, 0, sizeof hdr);
  hdr[0] = 'B'; hdr[1] = 'M';
  le32(hdr + 2, 54 + row * (uint32_t)h);
  le32(hdr + 10, 54);
  le32(hdr + 14, 40);
  le32(hdr + 18, (uint32_t)w);
  le32(hdr + 22, (uint32_t)h);
  hdr[26] = 1; hdr[28] = 24;
  le32(hdr + 34, row * (uint32_t)h);
  fwrite(hdr, 1, sizeof hdr, fp);
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      uint32_t px = rgba[(size_t)y * (size_t)w + (size_t)x]; /* RGBA bytes in memory order R,G,B,A */
      const uint8_t *b = (const uint8_t *)&px;
      line[x * 3] = b[2]; line[x * 3 + 1] = b[1]; line[x * 3 + 2] = b[0];
    }
    fwrite(line, 1, row, fp);
  }
  fclose(fp);
  free(rgba); free(line);
  g_dumps_written++;
  fprintf(stderr, "run_original: dump %s\n", path);
  return 0;
}

/* ---- script ---- */

static uint32_t now_ms(void) { return GetTickCount(); }

static const struct { const char *name; uint16_t vk; } k_vk[] = {
    {"VK_BACK", 0x08}, {"VK_TAB", 0x09}, {"VK_RETURN", 0x0D}, {"VK_ENTER", 0x0D}, {"VK_SHIFT", 0x10},
    {"VK_CONTROL", 0x11}, {"VK_MENU", 0x12}, {"VK_ESCAPE", 0x1B}, {"VK_SPACE", 0x20},
    {"VK_PRIOR", 0x21}, {"VK_NEXT", 0x22}, {"VK_END", 0x23}, {"VK_HOME", 0x24}, {"VK_LEFT", 0x25},
    {"VK_UP", 0x26}, {"VK_RIGHT", 0x27}, {"VK_DOWN", 0x28}, {"VK_INSERT", 0x2D}, {"VK_DELETE", 0x2E},
    {"VK_F1", 0x70}, {"VK_F2", 0x71}, {"VK_F3", 0x72}, {"VK_F4", 0x73}, {"VK_F5", 0x74},
    {"VK_F6", 0x75}, {"VK_F7", 0x76}, {"VK_F8", 0x77}, {"VK_F9", 0x78}, {"VK_F10", 0x79},
    {"VK_F11", 0x7A}, {"VK_F12", 0x7B},
};

static int parse_vk(const char *s, uint16_t *vk) {
  char *end;
  long v;
  for (size_t i = 0; i < sizeof k_vk / sizeof k_vk[0]; i++)
    if (strcmp(s, k_vk[i].name) == 0) { *vk = k_vk[i].vk; return 0; }
  if (s[0] && !s[1] && isalnum((unsigned char)s[0])) { *vk = (uint16_t)toupper((unsigned char)s[0]); return 0; }
  v = strtol(s, &end, 0);
  if (end == s || *end || v < 0 || v > 255) return -1;
  *vk = (uint16_t)v;
  return 0;
}

static void press(uint16_t vk) {
  rt_input_key(vk, 0, 1, now_ms());
  rt_input_key(vk, 0, 0, now_ms());
}

static void type_char(char ch) {
  uint16_t vk = 0;
  int shift = 0;
  if (ch >= 'a' && ch <= 'z') vk = (uint16_t)(ch - 'a' + 'A');
  else if (ch >= 'A' && ch <= 'Z') { vk = (uint16_t)ch; shift = 1; }
  else if (ch >= '0' && ch <= '9') vk = (uint16_t)ch;
  else if (ch == ' ') vk = 0x20;
  else if (ch == ',') vk = 0xBC;
  else if (ch == '.') vk = 0xBE;
  else if (ch == '-') vk = 0xBD;
  else if (ch == '/') vk = 0xBF;
  else if (ch == ';') vk = 0xBA;
  else if (ch == '\'') vk = 0xDE;
  else { fprintf(stderr, "run_original: cannot type a character (unsupported)\n"); return; }
  if (shift) rt_input_key(0x10, 0x2A, 1, now_ms());
  press(vk);
  if (shift) rt_input_key(0x10, 0x2A, 0, now_ms());
}

static void do_click(int16_t x, int16_t y, uint16_t btn) {
  rt_input_mouse(x, y, 0, now_ms());
  rt_input_mouse(x, y, btn, now_ms());
  rt_input_mouse(x, y, 0, now_ms());
}

static void advance_clock(long ms) {
  int y, mo, d, h, mi, s;
  long secs = (ms + 999) / 1000;
  rt_clock_get(&y, &mo, &d, &h, &mi, &s);
  /* rt_clock_set normalises out-of-range fields? Not guaranteed: do the carry by hand. */
  s += (int)secs;
  mi += s / 60; s %= 60;
  h += mi / 60; mi %= 60;
  d += h / 24; h %= 24;
  /* day overflow is ignored (scripts are short); clamp so the date stays valid */
  if (d > 28) d = 28;
  rt_clock_set(y, mo, d, h, mi, s);
}

/* user_wait_hook: runs script lines until one produces input (true) or the script is over. */
static bool script_hook(void) {
  while (g_next < g_nlines) {
    char *ln = g_lines[g_next++];
    char cmd[32] = "", a1[256] = "";
    int x, y;
    while (*ln == ' ' || *ln == '\t') ln++;
    if (!*ln || *ln == '#') continue;
    if (sscanf(ln, "%31s", cmd) != 1) continue;
    if (strcmp(cmd, "wait") == 0) {
      long ms = 0;
      if (sscanf(ln, "%*s %ld", &ms) != 1) { fprintf(stderr, "run_original: bad line: %s\n", ln); continue; }
      advance_clock(ms);
      return true; /* let timers / paints run */
    } else if (strcmp(cmd, "click") == 0 || strcmp(cmd, "rclick") == 0) {
      if (sscanf(ln, "%*s %d %d", &x, &y) != 2) { fprintf(stderr, "run_original: bad line: %s\n", ln); continue; }
      do_click((int16_t)x, (int16_t)y, cmd[0] == 'r' ? RT_BTN_RIGHT : RT_BTN_LEFT);
      return true;
    } else if (strcmp(cmd, "key") == 0) {
      /* key VK_MENU+F: every part but the last is a modifier held while the last key is pressed */
      uint16_t vk[4];
      int nvk = 0;
      char *tok, *save = NULL;
      if (sscanf(ln, "%*s %255s", a1) != 1) { fprintf(stderr, "run_original: bad line: %s\n", ln); continue; }
      for (tok = strtok_r(a1, "+", &save); tok && nvk < 4; tok = strtok_r(NULL, "+", &save))
        if (parse_vk(tok, &vk[nvk]) != 0) { nvk = 0; break; }
        else nvk++;
      if (nvk == 0) { fprintf(stderr, "run_original: bad line: %s\n", ln); continue; }
      for (int i = 0; i < nvk - 1; i++) rt_input_key(vk[i], 0, 1, now_ms());
      press(vk[nvk - 1]);
      for (int i = nvk - 2; i >= 0; i--) rt_input_key(vk[i], 0, 0, now_ms());
      return true;
    } else if (strcmp(cmd, "type") == 0) {
      const char *t = ln + 4;
      if (*t == ' ') t++;
      if (*t == '$') { /* type $NAME types the value of the environment variable (e.g. a serial kept out of the scripts) */
        t = getenv(t + 1);
        if (!t) { fprintf(stderr, "run_original: environment variable in a type line is not set; typing nothing\n"); t = ""; }
      }
      for (; *t; t++) type_char(*t);
      return true;
    } else if (strcmp(cmd, "dump") == 0) {
      if (sscanf(ln, "%*s %255s", a1) != 1) { fprintf(stderr, "run_original: bad line: %s\n", ln); continue; }
      dump_frame(a1);
    } else {
      fprintf(stderr, "run_original: unknown script command: %s\n", cmd);
    }
  }
  return false;
}

static int load_script(const char *path) {
  FILE *fp = fopen(path, "r");
  char buf[1024];
  if (!fp) { fprintf(stderr, "run_original: cannot open script %s\n", path); return -1; }
  while (fgets(buf, sizeof buf, fp) && g_nlines < MAX_LINES) {
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    g_lines[g_nlines++] = strdup(buf);
  }
  fclose(fp);
  return 0;
}

/* ---- diagnostics ---- */

/* RUN_ORIGINAL_MSGS=1: print every message the game dispatches (hwnd, message, wParam, lParam). */
static void h_dispatch(HCall *c) {
  uint32_t lp = (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16);
  if (getenv("RUN_ORIGINAL_MSGS") && lp && mem_check(lp, sizeof(MSG16))) {
    const MSG16 *m = (const MSG16 *)LIN(lp);
    fprintf(stderr, "run_original: msg hwnd=%04x msg=%04x wParam=%04x lParam=%08x\n", m->hwnd, m->message,
            m->wParam, m->lParam);
  }
  c->ret = DispatchMessage(lp);
}

static void trace_send(const char *what, HCall *c) {
  if (getenv("RUN_ORIGINAL_MSGS")) {
    uint32_t sp = MK_FP(c->regs->ss, c->regs->sp);
    const uint8_t *r = (const uint8_t *)LIN(sp);
    fprintf(stderr, "run_original: %s hwnd=%04x msg=%04x wParam=%04x lParam=%08x from %u:%04x\n", what,
            hcall_word(c, 4), hcall_word(c, 3), hcall_word(c, 2),
            (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16), (r[2] | (r[3] << 8)) >> 3,
            r[0] | (r[1] << 8));
  }
}
static void h_sendmsg(HCall *c) {
  trace_send("SendMessage", c);
  c->ret = SendMessage(hcall_word(c, 4), hcall_word(c, 3), hcall_word(c, 2),
                       (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16));
}
static void h_postmsg(HCall *c) {
  trace_send("PostMessage", c);
  c->ret = PostMessage(hcall_word(c, 4), hcall_word(c, 3), hcall_word(c, 2),
                       (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16));
}

/* RUN_ORIGINAL_WNDPROC=seg:off (hex offset, e.g. 4:0528): print the message arguments at every entry
 * of that far PASCAL window procedure. */
static void on_wndproc_entry(uint32_t lin, void *ud) {
  Regs16 r;
  const uint8_t *a;
  (void)lin;
  (void)ud;
  harness_regs(&r);
  a = (const uint8_t *)LIN(MK_FP(r.ss, (uint16_t)(r.sp + 4))); /* above the far return address */
  fprintf(stderr, "run_original: wndproc hwnd=%04x msg=%04x wParam=%04x lParam=%04x%04x\n",
          a[8] | (a[9] << 8), a[6] | (a[7] << 8), a[4] | (a[5] << 8), a[2] | (a[3] << 8), a[0] | (a[1] << 8));
}

/* PostQuitMessage wrapper: reports the caller (the far return address on the stack) so that the
 * end of a run can be traced to the original code that asked for it. */
static void h_postquit(HCall *c) {
  uint32_t sp = MK_FP(c->regs->ss, c->regs->sp);
  const uint8_t *r = (const uint8_t *)LIN(sp);
  uint16_t off = (uint16_t)(r[0] | (r[1] << 8)), seg = (uint16_t)(r[2] | (r[3] << 8));
  fprintf(stderr, "run_original: PostQuitMessage(%d) called from %04x:%04x (segment %u)\n",
          (int16_t)hcall_word(c, 0), seg, off, seg >> 3);
  PostQuitMessage((int16_t)hcall_word(c, 0));
}


static void report_state(const char *why) {
  Regs16 r;
  size_t n = harness_log_count();
  fprintf(stderr, "run_original: %s\n", why);
  harness_regs(&r);
  fprintf(stderr, "  regs: CS:IP=%04x:%04x SS:SP=%04x:%04x DS=%04x ES=%04x AX=%04x BX=%04x CX=%04x DX=%04x BP=%04x\n",
          r.cs, r.ip, r.ss, r.sp, r.ds, r.es, r.ax, r.bx, r.cx, r.dx, r.bp);
  fprintf(stderr, "  API calls so far: %zu; last 12:\n", n);
  for (size_t i = n > 12 ? n - 12 : 0; i < n; i++) {
    const HLogEntry *e = harness_log_get(i);
    fprintf(stderr, "    #%zu %s.%s (ord %u) ret=0x%x%s\n", i, e->module, e->name ? e->name : "?", e->ordinal,
            e->ret, e->implemented ? "" : "  [UNIMPLEMENTED]");
  }
  fprintf(stderr, "  dumps written: %d\n", g_dumps_written);
  print_log();
}

/* RUN_ORIGINAL_LOG=1 in the environment prints every API call with its raw argument bytes. */
static void print_log(void) {
  size_t n = harness_log_count();
  if (getenv("RUN_ORIGINAL_LOG")) {
    for (size_t i = 0; i < n; i++) {
      const HLogEntry *e = harness_log_get(i);
      fprintf(stderr, "  LOG %zu %s.%s args:", i, e->module, e->name ? e->name : "?");
      for (unsigned k = 0; k < e->argbytes && k < HARNESS_MAX_LOG_ARGS; k++) fprintf(stderr, " %02x", e->args[k]);
      fprintf(stderr, " ret=%x", e->ret);
      fprintf(stderr, "\n");
    }
  }
}

static void on_abort(int sig) {
  (void)sig;
  if (g_run_started) {
    report_state("ABORT (strict mode: unimplemented API, or an assertion in the runtime)");
    dump_frame("abort");
  }
  _exit(3);
}

static void on_alarm(int sig) {
  (void)sig;
  report_state("TIMEOUT");
  dump_frame("timeout");
  _exit(2);
}

/* --put HOSTFILE GUESTPATH: copies a host file into the virtual file system before the run
 * (for example a Stars.ini to C:\\WINDOWS\\STARS.INI). */
static int put_file(const char *host, const char *guest) {
  FILE *f = fopen(host, "rb");
  long n;
  uint8_t *b;
  int rc;
  if (!f) { fprintf(stderr, "run_original: cannot open %s\n", host); return -1; }
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  b = malloc((size_t)n + 1);
  if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return -1; }
  fclose(f);
  rc = vfs_put(guest, b, (size_t)n);
  free(b);
  if (rc != 0) fprintf(stderr, "run_original: vfs_put %s failed\n", guest);
  return rc;
}

static const char *g_unreg[16];
static int g_nunreg;

static const char *g_put_host[16], *g_put_guest[16];
static int g_nput;

static void usage(void) {
  fprintf(stderr,
          "usage: run_original [--strict] [--script FILE] [--dump DIR] [--exe FILE] [--max-api N]\n"
          "                    [--timeout SECONDS] [--list-imports] [--put HOSTFILE GUESTPATH]... [--unregister MODULE.ORDINAL]...\n");
}

int main(int argc, char **argv) {
  const char *exe = "original/stars26i.exe", *script = NULL;
  int strict = 0, list_imports = 0;
  unsigned max_api = 0;
  unsigned timeout = 0;
  FILE *fp;
  long len;
  uint8_t *img;
  int rc = 0;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--strict")) strict = 1;
    else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
    else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump_dir = argv[++i];
    else if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
    else if (!strcmp(argv[i], "--max-api") && i + 1 < argc) max_api = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) timeout = (unsigned)strtoul(argv[++i], NULL, 0);
    else if (!strcmp(argv[i], "--list-imports")) list_imports = 1;
    else if (!strcmp(argv[i], "--unregister") && i + 1 < argc && g_nunreg < 16) g_unreg[g_nunreg++] = argv[++i];
    else if (!strcmp(argv[i], "--put") && i + 2 < argc && g_nput < 16) {
      g_put_host[g_nput] = argv[i + 1];
      g_put_guest[g_nput++] = argv[i + 2];
      i += 2;
    }
    else { usage(); return 1; }
  }
  if (script && load_script(script) != 0) return 1;

  fp = fopen(exe, "rb");
  if (!fp) { fprintf(stderr, "run_original: cannot open %s\n", exe); return 1; }
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  img = malloc((size_t)len);
  if (!img || fread(img, 1, (size_t)len, fp) != (size_t)len) { fclose(fp); return 1; }
  fclose(fp);

  if (harness_load(exe) != 0) return 1;
  if (gdi_init() != 0 || user_init() != 0) { fprintf(stderr, "run_original: gdi/user init failed\n"); return 1; }
  if (kernel_load(img, (size_t)len) != 0) { fprintf(stderr, "run_original: kernel_load failed\n"); return 1; }
  /* The NE loader of real Windows initialises DGROUP's local heap just past the stack; the header
   * (nedump: heap/stack 8192 / 16384) gives its size. harness_load maps the room for it. */
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u));
  kernel_register();
  gdi_register();
  user_register();
  commdlg_register();
  toolhelp_win87em_register();
  harness_register("USER", 6, h_postquit);
  harness_register("USER", 114, h_dispatch);
  harness_register("USER", 111, h_sendmsg);
  harness_register("USER", 110, h_postmsg);
  free(img);
  for (int i = 0; i < g_nput; i++)
    if (put_file(g_put_host[i], g_put_guest[i]) != 0) return 1;

  if (list_imports) {
    size_t unreg = 0;
    for (size_t i = 0; i < harness_import_count(); i++) {
      const char *m, *nm;
      uint16_t o;
      harness_import_info(i, &m, &o, &nm);
      if (!harness_import_has_handler(i)) {
        printf("UNREGISTERED %s.%u %s\n", m, o, nm ? nm : "?");
        unreg++;
      }
    }
    printf("%zu imports, %zu unregistered\n", harness_import_count(), unreg);
    return 0;
  }

  /* --unregister MODULE.ORDINAL (regression proof: strict mode must then abort naming the API) */
  for (int i = 0; i < g_nunreg; i++) {
    char mod[32];
    unsigned ord;
    if (sscanf(g_unreg[i], "%31[^.].%u", mod, &ord) == 2) harness_register(mod, (uint16_t)ord, NULL);
  }
  harness_set_strict(strict != 0);
  user_wait_hook = script_hook;
  if (strict) signal(SIGABRT, on_abort);
  if (timeout) { signal(SIGALRM, on_alarm); alarm(timeout); }
  if (max_api) harness_stop_after_api(max_api);

  if (getenv("RUN_ORIGINAL_WNDPROC")) {
    unsigned seg = 0, off = 0;
    if (sscanf(getenv("RUN_ORIGINAL_WNDPROC"), "%u:%x", &seg, &off) == 2 && seg >= 1 && seg <= 35) {
      uint32_t lin = (uint32_t)((uint8_t *)LIN(MK_FP(MEM_SEL_SEG(seg), off)) - mem_arena());
      harness_trace(lin, lin + 1, on_wndproc_entry, NULL);
    }
  }
  g_run_started = 1;
  harness_call(MEM_SEL_SEG(34), 0x001A, HCONV_CDECL, NULL, 0, NULL);
  g_run_started = 0;

  if (kernel_fatal_pending) { report_state("FatalExit / FatalAppExit"); rc = 1; }
  else if (harness_stopped_early()) { report_state("stopped by --max-api"); rc = 2; }
  else if (kernel_exit_pending) {
    /* DOS terminate (INT 21h AH=4Ch) stops the emulation inside the thunk: not a fault. */
    fprintf(stderr, "run_original: game terminated (DOS exit code %d)%s\n", kernel_exit_code,
            user_input_exhausted ? " after the script ran out (WM_QUIT synthesised)" : "");
  } else if (harness_last_error() != 0) { report_state("emulation fault"); dump_frame("fault"); rc = 1; }
  else
    fprintf(stderr, "run_original: game returned from its entry point\n");
  fprintf(stderr, "run_original: %zu API calls, %d dumps\n", harness_log_count(), g_dumps_written);
  print_log();
  user_wait_hook = NULL;
  user_shutdown();
  kernel_shutdown();
  harness_shutdown();
  return rc;
}
