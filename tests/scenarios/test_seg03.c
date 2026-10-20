/* Diff tests for decompiled segment 3 (src/seg03.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 3 holds WinMain and the code around it: the command-line parser and the message loop
 * (3:0x0000), the "-b" batch-file reader (3:0x03e0), a duplicate-player search (3:0x04a2), the
 * system colour brushes (3:0x054c) and the program's global clean-up (3:0x0766), the "About" dialog
 * with its credits scroller (3:0x0ca0, helper 3:0x0c7a), the order-information dialog (3:0x0ec2), and
 * the two keyboard filters of the message loop (3:0x0f3a for WM_CHAR, 3:0x0f90 for WM_KEYDOWN and
 * WM_KEYUP). Every scenario builds its state by writing DGROUP directly or with the ORIGINAL code;
 * DIFF then snapshots it and compares the original with the C version (return value, all memory,
 * the ordered Win16 API calls).
 *
 * The runtime state the scenarios touch (windows, GDI objects, the message queue, the VFS, the global
 * heap) lives in the arena, so DIFF restores it between the ORIGINAL run and the C run. Callees that
 * are not decompiled and cannot run in isolation (the dialog and
 * painting helpers of segments 7, 8, 9, 10, 32, and the pop-up menu 24:0x0e24 and 31:0x61b0
 * that the toolbar functions of segment 13 call) are replaced in the code image by recording test
 * doubles (stub_install): the same bytes serve both runs, and each stubbed call is an entry of the
 * API log, compared in order with the real API calls. Scenarios never stub the function under test.
 * Segment 13 itself (13:0x0902, 0x09ae, 0x0fb8) and segment 4 (4:0x0000, 4:0x1f40) are decompiled
 * (src/seg13.c, src/seg04.c): they run for real in both runs (rule j2: never doubled), the ORIGINAL code
 * in one and the C version in the other.
 * See the comment above each scenario for what it does and does not reach. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/user.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define SEL3 MEM_SEL_SEG(3)
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }

static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* ---- test support: far memory, windows, callee test doubles ---- */

#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/vfs.h"

/* A zeroed, locked global block; returns its far pointer. */
static uint32_t gblock(uint16_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  return GlobalLock(h);
}
/* A NUL-terminated string in far memory (never freed: the arena is reset with the process). */
static uint32_t fstr(const char *s) {
  uint32_t p = gblock((uint16_t)(strlen(s) + 1));
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}

/* Windows. The class procedure is 3 instructions of test code: MOV AX,1; XOR DX,DX; RETF 10 (a PASCAL
 * window procedure that answers every message with 1), so creation is not vetoed and nothing else
 * happens. */
static uint16_t g_class_ok;
static uint16_t g_class_ctr;
static void register_test_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG03TEST");
  w->hInstance = 0x1234;
  g_class_ok = RegisterClass(wc) != 0;
  CHECK(g_class_ok);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG03TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy,
                            parent, id, 0x1234, 0);
  CHECK(h != 0);
  g_class_ctr++;
  return h;
}

/* Test doubles (docs/CONVENTIONS.md rule j2) for callees that are not decompiled yet and too large or
 * too state-hungry to run in isolation. The first 5 bytes of the callee in the code segment image
 * become JMP FAR to an import thunk of the module "STUBS" (ordinal = stub id, CDECL: the caller pops
 * the arguments), registered with harness_add_import. The handler returns the value the test stored
 * for that id (stub_ret). The same image serves the ORIGINAL run and the C version's CALL_ORIG, so
 * each stubbed call is an entry in the harness API log, in order with the real API calls the code
 * makes: DIFF compares that log (module, ordinal, the argument words as pushed, the return value)
 * entry by entry, so a stubbed call that is missing, repeated, made with another argument or made
 * before or after an API call or another stubbed call shows as a difference.
 * Not compared by the log alone: the memory a pointer argument points to, and the order of a stubbed
 * call against plain memory writes. For the memory a stub's pointer argument names, or that a write
 * must precede, register a watch (stub_watch): at every call of that stub the handler stores a hash
 * of the watched bytes in the stub block (compared as arena memory), so the content seen at call time
 * is checked. A stubbed call does nothing else (no side effect on the game state). The code under
 * test (the function being diffed) is never stubbed. */
#define STUB_LOG0 0x100
#define STUB_BLKSZ 0x4000
#define STUB_MAXW 8
static uint32_t g_stub_blk;
typedef struct StubWatch {
  unsigned id;
  bool ds;      /* off is a DGROUP offset, else fp is a far pointer */
  uint32_t fp;  /* far pointer, or the DGROUP offset when ds */
  uint16_t len;
} StubWatch;
static StubWatch g_stub_watch[STUB_MAXW];
static unsigned g_stub_nwatch;
static void stub_watch(unsigned id, bool ds, uint32_t fp, uint16_t len) {
  for (unsigned i = 0; i < g_stub_nwatch; i++) {
    if (g_stub_watch[i].id == id && g_stub_watch[i].ds == ds && g_stub_watch[i].fp == fp) {
      g_stub_watch[i].len = len;
      return;
    }
  }
  CHECK(g_stub_nwatch < STUB_MAXW);
  g_stub_watch[g_stub_nwatch++] = (StubWatch){id, ds, fp, len};
}
static void stub_handler(HCall *c) {
  uint8_t *blk = LIN(g_stub_blk);
  c->ret = (uint32_t)blk[0x12 + 4 * c->ordinal] | ((uint32_t)blk[0x13 + 4 * c->ordinal] << 8);
  c->ret = (c->ret << 16) | (uint32_t)blk[0x10 + 4 * c->ordinal] | ((uint32_t)blk[0x11 + 4 * c->ordinal] << 8);
  for (unsigned i = 0; i < g_stub_nwatch; i++) {
    const StubWatch *w = &g_stub_watch[i];
    if (w->id != c->ordinal) continue;
    const uint8_t *src = w->ds ? (const uint8_t *)LINN((uint16_t)w->fp) : (const uint8_t *)LIN(w->fp);
    uint32_t h = 2166136261u;
    for (uint16_t k = 0; k < w->len; k++) h = (h ^ src[k]) * 16777619u;
    uint16_t at;
    memcpy(&at, blk, 2);
    if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
    memcpy(blk + at, &h, 4);
    at = (uint16_t)(at + 4);
    memcpy(blk, &at, 2);
  }
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  *(uint16_t *)LIN(g_stub_blk) = STUB_LOG0;
}
static void stub_ret(unsigned id, uint16_t ax, uint16_t dx) {
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x10 + 4 * id))) = ax;
  *(uint16_t *)LIN(MK_FP(HI(g_stub_blk), (uint16_t)(0x12 + 4 * id))) = dx;
}
static void stub_install(uint16_t seg, uint16_t off, unsigned id, unsigned nargs) {
  char name[32];
  snprintf(name, sizeof name, "STUB_%u_%04x", seg, off);
  uint32_t th = harness_add_import("STUBS", (uint16_t)id, name, HK_VARARGS, (uint16_t)(2 * nargs), HR_RET32, stub_handler);
  CHECK(th != 0);
  uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
  memcpy(LIN(MK_FP(SEG_SEL(seg), off)), c, sizeof c);
}

/* The keyboard state seen by GetKeyState(VK_SHIFT). */
static void set_shift(bool down) {
  rt_input_key(VK_SHIFT, 0x2a, down ? 1 : 0, 0);
  uint32_t m = gblock(32);
  CHECK_EQ(PeekMessage(m, 0, 0x100, 0x108, 1), 1); /* the key state is applied when the message is read */
}

/* ---- 3:0x0c7a: format the About-box version string ---- */

static void t_0c7a(void) {
  DS_B(0x5866) = 0xEE; /* the buffer is overwritten with the formatted text */
  DIFF(3, 0x0c7a, FAR_CDECL);
}

/* ---- 3:0x0ec2: ORDERINFODLG, the order-information dialog procedure ---- */

static void t_0ec2(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  DS_W(0x0010) = GetStockObject(0); /* WHITE_BRUSH: the dialog background brush */
  ds_w32(0x253a, 0x00C0C0C0);
  /* WM_ERASEBKGND (0x14): GetClientRect + FillRect, returns 1 */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x14, dc, 0, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x14, 0, 0x1234, 0x5678);
  /* WM_CTLCOLOR (0x19): lParam high word 6 sets the background colour and returns the brush */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x19, dc, 6, 0x5);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x19, dc, 7, 0x5);   /* another control type: returns 0 */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x19, dc, 6, 0);     /* the type is the high word: lParam = 0x0006:0 */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x19, dc, 0, 6);     /* a 6 in the low word is not the type */
  /* WM_COMMAND (0x111): ids 1 and 2 end the dialog with 1 */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x111, 1, 0, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x111, 2, 0, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x111, 3, 0, 0);     /* any other id: 0 */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x111, 0, 0, 0);
  /* other messages */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x110, 1, 0, 0);     /* WM_INITDIALOG: not handled */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x0, 1, 0, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x13, 1, 0, 0);      /* just below WM_ERASEBKGND */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x15, 1, 0, 0);      /* just above */
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x18, 1, 6, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x1a, 1, 6, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x110, 1, 0, 0);
  DIFF(3, 0x0ec2, FAR_PASCAL, 0, 0x112, 1, 0, 0);
}

/* ---- 3:0x0f3a: WM_CHAR filter of the message loop ---- */

static void t_0f3a(void) {
  /* [0x150] is the window the "+", "-" characters are forwarded to; [0x158] the window whose edit
   * control keeps its own characters (compared with the focus window [0x59e6]). */
  const uint16_t keys[] = {0x2b, 0x2d, 0x76, 0x56, 0x2a, 0x2c, 0x2e, 0x75, 0x77, 0x55, 0x57, 0, 0xFFFF, 0x41};
  for (unsigned t = 0; t < 4; t++) {
    DS_W(0x0150) = (t & 1) ? 0x0001 : 0;  /* a main window, or none */
    DS_W(0x0158) = (t & 2) ? 0x0001 : 0;  /* an edit window, or none */
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++)
      DIFF(3, 0x0f3a, FAR_CDECL, 0x1234, keys[i], 0x5678, 0x9ABC);
  }
  /* the focus window equals [0x59e6] (both 0): the edit window keeps the character */
  DS_W(0x0150) = 1;
  DS_W(0x0158) = 1;
  DS_W(0x59e6) = 0;
  DIFF(3, 0x0f3a, FAR_CDECL, 0, 0x2b, 0, 0);
  DIFF(3, 0x0f3a, FAR_CDECL, 0, 0x76, 0, 0);
  DS_W(0x59e6) = 5; /* a different window has the focus: the character is forwarded */
  DIFF(3, 0x0f3a, FAR_CDECL, 0, 0x2b, 0, 0);
  DIFF(3, 0x0f3a, FAR_CDECL, 0, 0x56, 0x1111, 0x2222);
  DS_W(0x0150) = 0;
  DS_W(0x0158) = 0;
  DS_W(0x59e6) = 0;
}

/* ---- 3:0x04a2: find a player slot with the same name data as slot n ---- */

/* A dword the id check 14:0x2bb4 accepts, found with the ORIGINAL code (v / 36^4 must be 2, 4, 6 to 12,
 * 15, 18 or 22 and v % 36^4 between 100 and 0x170000: the search starts at 4 * 36^4 + 100). */
static uint32_t valid_id(uint32_t from) {
  for (uint32_t v = from;; v++) {
    const uint16_t a[] = {LO(v), HI(v)};
    if ((uint16_t)harness_call(MEM_SEL_SEG(14), 0x2bb4, HCONV_CDECL, a, 2, NULL) != 0) return v;
  }
}

static void t_04a2(void) {
  uint16_t h = GlobalAlloc(0x42, 0x1000);
  uint32_t tab = GlobalLock(h);
  CHECK(tab != 0);
  ds_w32(0x0980, tab);
  const uint32_t good = valid_id(0x00666864);
  uint32_t bad = 0;
  for (uint32_t v = 0x1000;; v++) {
    const uint16_t a[] = {LO(v), HI(v)};
    if ((uint16_t)harness_call(MEM_SEL_SEG(14), 0x2bb4, HCONV_CDECL, a, 2, NULL) == 0) { bad = v; break; }
  }
  /* 6 slots; the table at DS:0x980 has 16-byte entries: a key dword and 12 bytes of name data */
  DS_W(0x0078) = 6;
  for (unsigned j = 0; j < 6; j++) DS_B(0x5ab8 + 0xc0 * j) = 4; /* flag 4: the slot is in use */
  uint8_t *t = LIN(tab);
  memset(t, 0, 0x1000);
  for (unsigned j = 0; j < 6; j++) {
    uint32_t key = good + 0x11 * j;
    memcpy(t + 16 * j, &key, 4);
    memset(t + 16 * j + 4, (int)(0x20 + j), 11);
    uint32_t k2 = valid_id(key);
    memcpy(t + 16 * j, &k2, 4);
  }
  for (unsigned n = 0; n < 6; n++) DIFF(3, 0x04a2, FAR_CDECL, n); /* all distinct: -1 */
  /* slot 4 has slot 1's key and different name data: slot 1 is the answer for n = 4 */
  memcpy(t + 16 * 4, t + 16 * 1, 4);
  for (unsigned n = 0; n < 6; n++) DIFF(3, 0x04a2, FAR_CDECL, n);
  /* the same key and the same name data: not a match */
  memcpy(t + 16 * 4 + 4, t + 16 * 1 + 4, 11);
  for (unsigned n = 0; n < 6; n++) DIFF(3, 0x04a2, FAR_CDECL, n);
  /* differ only in each of the 11 compared bytes, and in the 12th byte (not compared) */
  for (unsigned b = 4; b < 16; b++) {
    memcpy(t + 16 * 4 + 4, t + 16 * 1 + 4, 11);
    t[16 * 4 + b] ^= 0x5A;
    DIFF(3, 0x04a2, FAR_CDECL, 4);
    DIFF(3, 0x04a2, FAR_CDECL, 1);
    DIFF(3, 0x04a2, FAR_CDECL, 0);
  }
  /* the slot flag: bit 4 clear in slot 1 hides it; other bits alone do not count */
  memcpy(t + 16 * 4 + 4, t + 16 * 1 + 4, 11);
  t[16 * 4 + 5] ^= 0x01;
  DS_B(0x5ab8 + 0xc0 * 1) = 0xFB;
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  DS_B(0x5ab8 + 0xc0 * 1) = 0x04;
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  DS_B(0x5ab8 + 0xc0 * 1) = 0xFF;
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  DS_B(0x5ab8 + 0xc0 * 1) = 0x04;
  /* an invalid key in the slot asked for: -1 without a search */
  memcpy(t + 16 * 4, &bad, 4);
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  memcpy(t + 16 * 4, t + 16 * 1, 4);
  /* a count of 0 and of 1: the loop body is skipped / sees only the asked slot */
  DS_W(0x0078) = 0;
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  DS_W(0x0078) = 1;
  DIFF(3, 0x04a2, FAR_CDECL, 0);
  DIFF(3, 0x04a2, FAR_CDECL, 1);
  DS_W(0x0078) = 0xFFFF; /* a negative count: the signed compare ends the search at once */
  DIFF(3, 0x04a2, FAR_CDECL, 4);
  DS_W(0x0078) = 6;
  /* every index the 4 KB table holds, past the filled slots too (zero keys fail the id check) */
  for (unsigned n = 0; n < 256; n++) DIFF(3, 0x04a2, FAR_CDECL, n);
  DS_W(0x0078) = 0;
  ds_w32(0x0980, 0);
  CHECK_EQ(GlobalUnlock(h), 0);
  CHECK_EQ(GlobalFree(h), 0);
}


/* ---- 3:0x03e0: read the first line of the batch file (-b) into the file name buffer ---- */

/* The file 3:0x03e0 opens is named in DS:0x5764 and read whole through 14:0x3100 / 34:0x03b6 / 12:0x0232
 * (heap 9) / 14:0x31da, then closed by 14:0x31c2; the first line (up to LF, or the end of the data)
 * replaces the name, with the last copied byte (the CR of a CR LF pair) overwritten by NUL.
 * NOT covered: the error paths where 14:0x3100 (file missing), 14:0x31da (short read) or 12:0x0232 (no
 * memory) throw to the catch buffer at DS:0x6E (34:0x0825). In the original, the throw resumes in the
 * caller of 34:0x0800; the C version cannot be resumed there from inside CALL_ORIG (see the note on
 * 12:0x0000 in src/seg12.c), so the scenarios only use files that exist and can be read. */
static void put_file(const char *name, const void *data, size_t len) {
  char path[64];
  snprintf(path, sizeof path, "C:\\STARS\\%s", name);
  CHECK_EQ(vfs_put(path, data, len), 0);
}
static void set_name(const char *name) {
  size_t n = strlen(name) + 1;
  memcpy(LINN(0x5764), name, n);
}
static void t_03e0(void) {
  struct { const char *text; size_t len; } c[] = {
      {"alpha.hst\r\nsecond line\r\nthird\r\n", 32},
      {"single\n", 7},
      {"no newline at the end", 21},
      {"\nstarts with a newline", 22},
      {"", 0},
      {"x", 1},
      {"\r\n", 2},
      {"a", 1},
      {"\n", 1},
      {"two\nlines\n", 10},
      {"lf only name\nrest", 17},
      {"\r\r\r\n", 4},
  };
  char longline[200];
  memset(longline, 'L', sizeof longline - 1);
  longline[120] = '\n';
  longline[sizeof longline - 1] = 0;
  for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
    put_file("BATCH.TXT", c[i].text, c[i].len);
    set_name("batch.txt");
    DIFF(3, 0x03e0, FAR_CDECL);
  }
  put_file("BATCH.TXT", longline, 199);
  set_name("batch.txt");
  DIFF(3, 0x03e0, FAR_CDECL);
  /* the name buffer and a stale read pointer from an earlier call differ */
  put_file("BATCH.TXT", "name.hst\r\n", 10);
  DS_W(0x015e) = 0x1234;
  DS_W(0x0160) = 0x4321;
  set_name("BATCH.TXT");
  DIFF(3, 0x03e0, FAR_CDECL);
  /* a name in another directory form */
  put_file("B2.TXT", "other\n", 6);
  set_name("c:\\stars\\b2.txt");
  DIFF(3, 0x03e0, FAR_CDECL);
}

/* ---- 3:0x054c: (re)create the system colour brushes ---- */

static void t_054c(void) {
  /* The eight brush slots and the colours the function caches. */
  static const uint16_t slots[8] = {0x10, 0x14, 0x16, 0x12, 0x1a, 0x18, 0x1c, 0x1e};
  uint16_t ba = GlobalAlloc(0x42, 0x500), bb = GlobalAlloc(0x42, 0x500);
  CHECK(ba != 0 && bb != 0);
  for (unsigned i = 0; i < 8; i++) DS_W(slots[i]) = 0;
  DS_W(0x0c7c) = 0;
  DS_W(0x5a4e) = 0;
  DIFF(3, 0x054c, FAR_CDECL); /* nothing to release */
  for (unsigned i = 0; i < 8; i++) { /* exactly one old brush in each slot */
    for (unsigned k = 0; k < 8; k++) DS_W(slots[k]) = 0;
    DS_W(slots[i]) = CreateSolidBrush(0x00112233u + i);
    CHECK(DS_W(slots[i]) != 0);
    DIFF(3, 0x054c, FAR_CDECL);
  }
  for (unsigned i = 0; i < 8; i++) DS_W(slots[i]) = CreateSolidBrush(0x00445566u + i);
  DIFF(3, 0x054c, FAR_CDECL); /* all slots hold a brush */
  DS_W(0x0c7c) = ba; /* the two resource blocks whose colour bytes are patched */
  DIFF(3, 0x054c, FAR_CDECL);
  DS_W(0x5a4e) = bb;
  DIFF(3, 0x054c, FAR_CDECL);
  DS_W(0x0c7c) = 0;
  DIFF(3, 0x054c, FAR_CDECL);
  DS_W(0x5a4e) = 0;
  for (unsigned i = 0; i < 8; i++) DS_W(slots[i]) = 0;
}

/* ---- 3:0x0f90: WM_KEYDOWN / WM_KEYUP filter of the message loop ---- */

static void reset_ds90(void) {
  static const uint16_t w[] = {0x14e, 0xbbe, 0x177e, 0x15a, 0x150, 0x158, 0x5a48, 0x5a4a, 0x5a4c, 0x539e, 0x5440,
                               0x59e6, 0x53e0, 0x83a, 0x54fe, 0x54f2, 0x54fa, 0x55e, 0x4b74, 0x4b7a, 0x4bee,
                               0x4bf0, 0x4bf2};
  for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) DS_W(w[i]) = 0;
}
/* Switches toolbar item `view` on or off in the view flags DS:0x055e (13:0x0902 reads them; item 8 also
 * needs DS:0x4c76 == 0xf and bit 0x40). */
static void view_set(uint16_t view, bool on) {
  uint16_t bit = view == 7 ? 0x20 : view == 8 ? 0x40 : view == 9 ? 0x80 : view == 0x0b ? 0x400 : 0x1000;
  DS_W(0x055e) = on ? (uint16_t)(DS_W(0x055e) | bit) : (uint16_t)(DS_W(0x055e) & ~bit);
  DS_W(0x4c76) = on ? 0x000f : 0x0000;
}
static void k90(uint16_t msg, uint16_t key) {
  DIFF(3, 0x0f90, FAR_CDECL, 0x1111, msg, key, 0x4321, 0x8765);
}

static void t_0f90(void) {
  /* windows: WA (active top-level) with child P and grandchild G; WB another top-level; D with a child
   * of control id 0x10b */
  uint16_t WB = mkwin(WS_OVERLAPPED | WS_VISIBLE, 400, 300, 100, 100, 0, 0);
  uint16_t D = mkwin(WS_OVERLAPPED | WS_VISIBLE, 500, 300, 100, 100, 0, 0);
  uint16_t D10b = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, D, 0x10b);
  uint16_t WA = mkwin(WS_OVERLAPPED | WS_VISIBLE, 0, 0, 300, 300, 0, 0);
  uint16_t P = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 200, 200, WA, 0x20);
  uint16_t G = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, P, 0x21);
  CHECK_EQ(GetActiveWindow(), WA);
  SetFocus(G);
  CHECK_EQ(GetFocus(), G);
  CHECK_EQ(GetParent(G), P);
  CHECK_EQ(GetParent(P), WA);
  CHECK_EQ(GetDlgItem(D, 0x10b), D10b);

  /* -- ESC down: the three ways out -- */
  reset_ds90();
  DS_W(0x14e) = WA; /* the dialog is the active window: destroyed */
  k90(0x100, 0x1b);
  DS_W(0x14e) = WB; /* not the active window: the next test */
  k90(0x100, 0x1b);
  DS_W(0xbbe) = WB; /* a window to cancel: WM_LBUTTONUP is sent */
  k90(0x100, 0x1b);
  DS_W(0x14e) = 0;
  k90(0x100, 0x1b);
  DS_W(0xbbe) = 0;
  DS_W(0x177e) = 0x7777; /* an invalid handle: DestroyWindow reports 0 and the call still returns 1 */
  k90(0x100, 0x1b);
  DS_W(0x177e) = WB; /* a real window: destroyed (the DIFF restores the runtime state) */
  k90(0x100, 0x1b);
  DS_W(0x177e) = 0;
  k90(0x100, 0x1b); /* nothing set: 0x1b is not an editing key either */
  DS_W(0x14e) = WA;
  DS_W(0xbbe) = WB;
  k90(0x100, 0x1c); /* only 0x1b triggers any of these */
  k90(0x100, 0x1a);
  reset_ds90();

  /* -- key up: ESC and Enter reach 13:0xfb8 only from a child of the window DS:0x15a -- */
  const uint16_t ups[] = {0x1b, 0x0d, 0x41, 0x0e, 0x1c};
  const uint16_t owners[] = {0, P, WA, WB, G};
  for (unsigned o = 0; o < 5; o++) {
    DS_W(0x15a) = owners[o];
    for (unsigned k = 0; k < 5; k++) k90(0x101, ups[k]);
  }
  reset_ds90();
  k90(0x101, 0x1b); /* [0x15a] zero */

  /* -- SHIFT: WM_SETCURSOR when the window under the cursor is DS:0x150 -- */
  rt_input_mouse(150, 150, 0, 0);
  uint32_t pt = gblock(8);
  GetCursorPos(pt);
  CHECK_EQ(*(int16_t *)LIN(pt), 150);
  uint16_t under = WindowFromPoint(*(uint32_t *)LIN(pt));
  CHECK(under != 0);
  DS_W(0x150) = under;
  k90(0x101, 0x10);
  k90(0x100, 0x10);
  k90(0x102, 0x10);
  DS_W(0x150) = WB; /* another window: nothing sent */
  k90(0x101, 0x10);
  k90(0x100, 0x10);
  DS_W(0x150) = 0;
  k90(0x101, 0x10);
  k90(0x100, 0x10);
  k90(0x102, 0x41); /* other messages return 0 */
  k90(0x104, 0x11);
  k90(0x0, 0x28);
  reset_ds90();

  /* -- the key filter: every key from 0 to 0xff with no focus rule -- */
  for (unsigned key = 0; key < 0x100; key++) k90(0x100, (uint16_t)key);
  k90(0x100, 0xFFFF);
  k90(0x100, 0x8000);
  k90(0x100, 0x01DB); /* 0xdb in the low byte only is not 0xdb */

  /* -- focus rules (they apply only when DS:0x158 is set) -- */
  DS_W(0x158) = WA;
  const uint16_t probes[] = {0x28, 0x08, 0x31, 0xbc, 0xdb};
  /* the window with the focus is DS:0x15a, a child, or a grandchild of it */
  const uint16_t owner2[] = {0, G, P, WA, WB};
  for (unsigned o = 0; o < 5; o++) {
    DS_W(0x15a) = owner2[o];
    for (unsigned k = 0; k < 5; k++) k90(0x100, probes[k]);
  }
  DS_W(0x15a) = 0;
  /* each of the windows that keep the keyboard for themselves */
  static const uint16_t guards[] = {0x5a48, 0x5a4a, 0x5a4c, 0x539e, 0x5440, 0x59e6, 0x53e0, 0x83a, 0x54fe, 0x54f2};
  for (unsigned g = 0; g < sizeof guards / sizeof guards[0]; g++) {
    DS_W(guards[g]) = G;
    k90(0x100, 0x28);
    k90(0x100, 0x31);
    DS_W(guards[g]) = WB; /* a different window: the rule does not apply */
    k90(0x100, 0x28);
    DS_W(guards[g]) = 0;
  }
  SetFocus(D10b); /* the control 0x10b of the dialog DS:0x14e has the focus */
  DS_W(0x14e) = D;
  k90(0x100, 0x28);
  k90(0x100, 0x31);
  DS_W(0x14e) = WB; /* a dialog without such a control */
  k90(0x100, 0x28);
  DS_W(0x14e) = 0;
  k90(0x100, 0x28);
  SetFocus(G);
  DS_W(0x14e) = D;
  k90(0x100, 0x28); /* another control has the focus */
  DS_W(0x14e) = 0;
  DS_W(0x158) = 0;

  /* -- cursor keys: forwarded to DS:0x158 unless the focus is the window DS:0x54fa -- */
  static const uint16_t cursor[] = {0x28, 0x26, 0x24, 0x23};
  for (unsigned k = 0; k < 4; k++) {
    DS_W(0x158) = WA;
    k90(0x100, cursor[k]);
    DS_W(0x54fa) = G;
    k90(0x100, cursor[k]);
    DS_W(0x54fa) = WB;
    k90(0x100, cursor[k]);
    DS_W(0x54fa) = 0;
    DS_W(0x158) = 0;
    k90(0x100, cursor[k]);
  }
  reset_ds90();

  /* -- delete and backspace: the editor in state 2 gets 10:0x606a(8) -- */
  static const uint16_t states[] = {0, 1, 2, 3, 0xFFFF};
  for (unsigned t = 0; t < 5; t++) {
    DS_W(0x4b74) = states[t];
    k90(0x100, 0x2e);
    k90(0x100, 0x08);
  }
  reset_ds90();

  /* -- digits 1 to 6: choose the view unless it is already chosen; 0, 7, 8, 9 use 13:0x902 (real code
   * now: the item is on when its bit of the view flags DS:0x55e is set, 8 also needs DS:0x4c76 == 0xf) -- */
  DS_W(0x15a) = P;
  for (unsigned cur = 0; cur < 16; cur += 7) {
    for (unsigned key = 0x30; key <= 0x39; key++) {
      for (unsigned on = 0; on < 2; on++) {
        DS_W(0x55e) = (uint16_t)(0x1230 | cur);
        view_set(key == 0x39 ? 9 : key == 0x38 ? 8 : key == 0x37 ? 7 : 0x0b, on != 0);
        k90(0x100, (uint16_t)key);
      }
    }
  }
  DS_W(0x55e) = 0x0003; /* '4' (0x34) is the current view: no change */
  k90(0x100, 0x34);
  DS_W(0x55e) = 0xFFF3;
  k90(0x100, 0x34);
  DS_W(0x55e) = 0x0004;
  k90(0x100, 0x34);
  DS_W(0x55e) = 0x0001;
  DS_W(0x15a) = 0;
  k90(0x100, 0x35);
  DS_W(0x15a) = 0x7777; /* InvalidateRect on a handle that is no window does nothing */
  k90(0x100, 0x35);
  DS_W(0x15a) = P;
  set_shift(true); /* '0' with SHIFT held selects 0x11, without it 0x0b */
  CHECK(GetKeyState(VK_SHIFT) < 0);
  for (unsigned i = 0; i < 2; i++) {
    DS_W(0x55e) = 0x1230;
    view_set(0x11, i != 0);
    k90(0x100, 0x30);
  }
  set_shift(false);
  CHECK(GetKeyState(VK_SHIFT) >= 0);
  for (unsigned i = 0; i < 2; i++) {
    DS_W(0x55e) = 0x1230;
    view_set(0x0b, i != 0);
    k90(0x100, 0x30);
  }
  reset_ds90();

  /* -- ',' and '.': step the high nibble of byte 10 of record DS:0x4b7a (or 1) in the table at
   * DS:0x4bf2:0x4bf0 -- */
  uint32_t tab = gblock(0x12 * 8);
  uint8_t *t = LIN(tab);
  DS_W(0x4bf0) = LO(tab);
  DS_W(0x4bf2) = HI(tab);
  stub_watch(5, false, tab, 0x12 * 8); /* the nibble is written before 7:0x153e and 9:0x07e0 are called */
  stub_watch(6, false, tab, 0x12 * 8);
  static const uint16_t nib[] = {0x00, 0x05, 0x0a, 0x0b, 0xff};
  static const uint16_t sel[] = {0, 1, 3, 0xFFFF};
  static const uint16_t cnt[] = {0, 2};
  for (unsigned n = 0; n < sizeof nib / sizeof nib[0]; n++) {
    for (unsigned r = 0; r < 8; r++) t[0x12 * r + 0xa] = (uint8_t)(nib[n] ^ (r << 4));
    for (unsigned a = 0; a < sizeof sel / sizeof sel[0]; a++) {
      for (unsigned b = 0; b < sizeof cnt / sizeof cnt[0]; b++) {
        DS_W(0x4b74) = 2;
        DS_W(0x4b7a) = sel[a];
        DS_W(0x4bee) = cnt[b];
        k90(0x100, 0xbc);
        k90(0x100, 0xbe);
      }
    }
  }
  DS_W(0x4b7a) = 0;     /* the count alone: DS:0x4bee above 1 (signed) and not above 1 */
  for (unsigned c = 0; c < 4; c++) {
    static const uint16_t cc[] = {1, 2, 0x7FFF, 0x8000};
    DS_W(0x4bee) = cc[c];
    k90(0x100, 0xbc);
  }
  DS_W(0x4b74) = 1; /* not in state 2: nothing happens */
  DS_W(0x4b7a) = 1;
  k90(0x100, 0xbc);
  k90(0x100, 0xbe);
  reset_ds90();

  /* -- '[' and ']' -- */
  k90(0x100, 0xdb);
  k90(0x100, 0xdd);
  reset_ds90();
  SetFocus(G);
  (void)DestroyWindow(WA); /* children go with their parent */
  (void)DestroyWindow(WB);
  (void)DestroyWindow(D);
}

/* ---- 3:0x0ca0: ABOUT, the About dialog procedure ---- */

/* The dialog is an ordinary window of the test class with the two static controls the procedure
 * addresses (0x401 for the version text, 0x41f for the credits scroller). WM_COMMAND with the id 0x76
 * opens the order-information dialog through DialogBox (instance DS:0x54F4, template 0x61). */
static void t_0ca0(void) {
  uint16_t dc = gdi_create_screen_dc();
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t st1 = mkwin(WS_CHILD | WS_VISIBLE, 5, 5, 200, 20, dlg, 0x401);
  uint16_t st2 = mkwin(WS_CHILD | WS_VISIBLE, 5, 40, 200, 56, dlg, 0x41f);
  CHECK(dc != 0 && st1 != 0 && st2 != 0);
  DS_W(0x0010) = GetStockObject(0);
  DS_W(0x2930) = GetStockObject(16);
  ds_w32(0x253a, 0x00C0C0C0);
  ds_w32(0x2870, 0x00800000);
  DS_W(0x267a) = 14;
  DS_W(0x54f4) = 0;
  const uint16_t ids[] = {dlg, 0, 0x7777};
  /* WM_ERASEBKGND (0x14), WM_CTLCOLOR (0x19) */
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x14, dc, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, 0, 0x14, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x19, dc, 6, 1);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x19, dc, 5, 1);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x19, dc, 0, 6);
  /* WM_INITDIALOG (0x110) */
  for (unsigned i = 0; i < 3; i++) {
    DS_W(0x5a56) = 0x1234;
    DS_W(0x5390) = 0x4321;
    DS_W(0x0162) = 0x7777;
    DIFF(3, 0x0ca0, FAR_PASCAL, ids[i], 0x110, 0, 0, 0);
  }
  /* WM_COMMAND (0x111) */
  DS_W(0x0162) = 14;
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 1, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 2, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 3, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 0x77, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 0x76, 0, 0);
  DS_W(0x54f4) = 0x1234;
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x111, 0x76, 0, 0);
  DS_W(0x54f4) = 0;
  /* other messages */
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x0, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x112, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x114, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x13, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x15, 0, 0, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x18, 0, 6, 0);
  DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x1a, 0, 6, 0);
  /* WM_TIMER (0x113): the scroller. [0x5390] is the pixel offset inside a line, [0x267a] the line
   * height and [0x5a56] the first visible line (-11 to 70). */
  static const uint16_t offs[] = {0, 1, 12, 13, 14, 15, 0x7FFF, 0x8000, 0xFFFE};
  static const uint16_t firsts[] = {0xfff5, 0xfff6, 0xfff0, 0, 1, 0x30, 0x3b, 0x3c, 0x45, 0x46, 0x47, 0x7FFF, 0x8000};
  static const uint16_t heights[] = {14, 2, 1, 0, 0x8000};
  for (unsigned h = 0; h < 5; h++) {
    DS_W(0x267a) = heights[h];
    for (unsigned o = 0; o < 9; o++) {
      DS_W(0x5390) = offs[o];
      for (unsigned f = 0; f < 13; f++) {
        if (h > 1 && (o > 3 || f > 6)) continue;
        DS_W(0x5a56) = firsts[f];
        DIFF(3, 0x0ca0, FAR_PASCAL, dlg, 0x113, 0, 0, 0);
      }
    }
  }
  DS_W(0x267a) = 14;
  DS_W(0x5390) = 4;
  DS_W(0x5a56) = 3;
  DIFF(3, 0x0ca0, FAR_PASCAL, 0, 0x113, 0, 0, 0);      /* no dialog: GetDlgItem gives 0 */
  DIFF(3, 0x0ca0, FAR_PASCAL, st1, 0x113, 0, 0, 0);    /* a window without the control 0x41f */
  DestroyWindow(dlg);
}

/* ---- 3:0x0766: the program's clean-up ---- */

/* Every slot it reads is given a distinct value so a wrong address or order shows in the API log;
 * the zero state exercises the "if the slot is set" tests the other way. The heap pointers and the
 * heap list are built with the original heap functions, so 12:0x04d0 and 12:0x01b0 free real blocks. */
static void fill_ds(uint16_t from, uint16_t to) {
  for (unsigned a = from; a < to; a += 2) {
    uint16_t v = (uint16_t)(0x2000 + a / 2 * 3);
    memcpy(LINN(a), &v, 2);
  }
}
static void t_0766(void) {
  /* The scenarios overwrite DGROUP's data (0x10 to 0x6700, except the heap table at 0xD74 to 0xDBC and
   * the two heap pointers); the original contents are put back afterwards for the other tests. */
  enum { LO_ = 0x10, HI_ = 0x6700 };
  static uint8_t saved[HI_ - LO_];
  memcpy(saved, LINN(LO_), sizeof saved);
  struct { uint16_t lo, hi; } keep[] = {{0x0d74, 0x0dbc}, {0x096c, 0x0970}, {0x0aac, 0x0ab0}};
#define KEPT(a) (((a) >= keep[0].lo && (a) < keep[0].hi) || ((a) >= keep[1].lo && (a) < keep[1].hi) || \
                 ((a) >= keep[2].lo && (a) < keep[2].hi))
  /* zero state: every "is the slot set" test takes the other way */
  for (unsigned a = LO_; a < HI_; a += 2) {
    uint16_t z = 0;
    if (!KEPT(a)) memcpy(LINN(a), &z, 2);
  }
  ds_w32(0x096c, 0);
  ds_w32(0x0aac, 0);
  DIFF(3, 0x0766, FAR_CDECL);
  /* heaps: a segment in heaps 0, 3, 6 and 9 and one block in heap 4 and in heap 2 */
  for (unsigned h = 0; h < 12; h += 3) {
    const uint16_t a[] = {0x40, (uint16_t)h};
    harness_call(MEM_SEL_SEG(12), 0x0232, HCONV_CDECL, a, 2, NULL);
  }
  { const uint16_t a[] = {0x30, 4}; ds_w32(0x096c, harness_call(MEM_SEL_SEG(12), 0x0232, HCONV_CDECL, a, 2, NULL)); }
  { const uint16_t a[] = {0x30, 2}; ds_w32(0x0aac, harness_call(MEM_SEL_SEG(12), 0x0232, HCONV_CDECL, a, 2, NULL)); }
  DIFF(3, 0x0766, FAR_CDECL);
  /* every slot set to its own value */
  for (unsigned a = LO_; a < HI_; a += 2) {
    if (!KEPT(a)) fill_ds((uint16_t)a, (uint16_t)(a + 2));
  }
  DIFF(3, 0x0766, FAR_CDECL);
#undef KEPT
  memcpy(LINN(LO_), saved, sizeof saved);
}

/* ---- 3:0x0000: WinMain ---- */

/* WinMain is run with its callees that are neither decompiled here nor self-contained replaced by test
 * doubles (stub_install above): 8:0x86cc (random seed), 8:0x8d94 (fatal box) and 8:0xb28a (the -p name
 * converter). The window classes 4:0x0000, the game start-up 1:0x0000 and the window creation 1:0x0af8
 * (with the ini reader 1:0x0d90 and the helpers they call) were doubles until segments 4 and 1 were
 * decompiled (stars-bfj.6, stars-bfj.3); they run for real in both runs. The scenarios with a previous
 * instance (hPrevInstance 1, 4:0x0000 not called) use the class "starsframe" registered by the test
 * (register_frame_class, a procedure that answers 1 to everything); a first instance (hPrevInstance 0)
 * runs the real 4:0x0000; where 1:0x0000 succeeds it also creates the main window with the real FRAMEWNDPROC.
 * The message 0x464, which is then in the queue, is dispatched to the real FRAMEWNDPROC by the message loop; the
 * by-name DispatchMessage of the C version (harness/byname.h) lowers the emulated stack pointer by 8 (message
 * pointer and return address) like the import thunk of the ORIGINAL run (stars-n7l; without it the stack addresses
 * the procedure passes to GetClientRect differ by 8 and DIFF fails). The start-up allocates about 2.3 MB of global
 * blocks, more than the 2 MB DIFF snapshots beyond the highest block, so that scenario first leaves a free hole
 * below a pinned block. Everything else is the real code: the
 * command-line parser, the option letters, 3:0x03e0 (-b),
 * 3:0x054c, the message loop with the real runtime queue (GetMessage, TranslateAccelerator,
 * TranslateMessage, DispatchMessage), 3:0x0f90 and 3:0x0f3a, and the clean-up 3:0x0766. The loop ends
 * at a WM_QUIT the scenario posts; the window DS:0x2806 belongs to the test class (hidden, so it
 * never receives WM_PAINT).
 * NOT covered: the throw path of the -b option (a missing batch file; see 3:0x03e0). 3:0x054c cannot
 * return 0, so the fatal box after it is unreachable (the `je 0x45` at 3:0x007a). The main window
 * is the real one made by 1:0x0af8 (hidden again at its end), so DS:0x2806 is no longer the test
 * window; the iconic-window branch of the accelerator scenarios is reached with nCmdShow = 2. */
static uint16_t g_wm_win, g_wm_icon, g_wm_other;
static void drain_queue(void) {
  uint32_t m = gblock(32);
  for (int i = 0; i < 2000 && PeekMessage(m, 0, 0, 0, 1); i++) {}
  CHECK_EQ(PeekMessage(m, 0, 0, 0, 0), 0);
}
static void wm_state(void) {
  static const uint16_t w[] = {0x312, 0x2666, 0x4c72, 0x150, 0x158, 0x15a, 0x14e, 0xbbe, 0x177e, 0x59e6,
                               0x4b74, 0x55e, 0x2526, 0x320, 0x322};
  for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) DS_W(w[i]) = 0;
  DS_W(0x2806) = g_wm_win;
  stub_ret(15, 0x1234, 0x5678);
}
static uint16_t g_wm_show = 1; /* nCmdShow: 2 makes 1:0x0af8 create the main window iconic (and hidden) */
static void wm(const char *cmd, uint16_t prev, int16_t quit) {
  uint32_t cl = fstr(cmd);
  PostQuitMessage(quit);
  DIFF(3, 0x0000, FAR_PASCAL, 0x0118, prev, HI(cl), LO(cl), g_wm_show);
  drain_queue();
}
static void load_module(void) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  CHECK_EQ(kernel_load(img, (size_t)len), 0); /* resources (the accelerator tables) need the module */
  free(img);
}
/* A copy of the module in which the bitmaps have another resource type (RT_BITMAP 2 becomes 0xf2 in the NE
 * resource table): every LoadBitmap fails, so the game start-up 1:0x0000 fails. */
static void load_module_without_bitmaps(void) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  uint32_t ne;
  uint16_t res;
  memcpy(&ne, img + 0x3c, 4);
  memcpy(&res, img + ne + 0x24, 2);
  for (uint8_t *t = img + ne + res + 2;;) { /* TYPEINFO: type, count, reserved (4), then 12 bytes per resource */
    uint16_t type, count;
    memcpy(&type, t, 2);
    memcpy(&count, t + 2, 2);
    if (type == 0) break;
    if (type == 0x8002) {
      type = 0x80f2;
      memcpy(t, &type, 2);
    }
    t += 8 + 12u * count;
  }
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}
/* The class of the main window of a previous instance (the real 4:0x0000 registers it with FRAMEWNDPROC, see the
 * note above WinMain): named by the string at DS:0x0196 ("starsframe"); its procedure answers every message
 * with 1 (MOV AX,1; XOR DX,DX; RETF 10). Registered, it also makes the first RegisterClass of the real 4:0x0000
 * fail (the class exists already). */
static void register_frame_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = MK_FP(MEM_SEL_DGROUP, 0x0196);
  w->hInstance = 0x0118;
  CHECK(RegisterClass(wc) != 0);
}
static void t_0000(void) {
  /* docs/functions.csv gives WinMain 250 bytes: Ghidra ended the function at the jump table (0x00FA).
   * The real body runs to 0x03DF (the clean-up call at 0x03d3, RETF 0xA at 0x03dc); count coverage
   * over all of it. The 112 bytes of the jump table disassemble to dead instructions. */
  difftest_set_extent(3, 0x0000, 0x03df, "WinMain");
  load_module();
  g_wm_win = mkwin(WS_OVERLAPPED, 0, 0, 100, 100, 0, 0);
  g_wm_other = mkwin(WS_OVERLAPPED, 0, 0, 100, 100, 0, 0);
  g_wm_icon = mkwin(WS_OVERLAPPED, 0, 0, 100, 100, 0, 0);
  (void)SetWindowLong(g_wm_icon, -16, GetWindowLong(g_wm_icon, -16) | 0x20000000UL); /* WS_MINIMIZE, still hidden */
  CHECK(IsIconic(g_wm_icon));
  CHECK(!IsIconic(g_wm_win));
  wm_state();
  put_file("BATCH.TXT", "from_batch.hst\r\nsecond\r\n", 24);

  static const char *const cmds[] = {
      "", " ", "   ", "game", "  game.hst  other", "game -a", "-a", "/a", "-A -a", "-w -t", "-W -T -x -X -h -H",
      "-c", "game -c", "-c game", "-C -c", "game -C", "-b batch.txt", "-B   batch.txt rest", "-bbatch.txt",
      "-b batch.txt -c", "-a -b batch.txt -w", "x -b batch.txt", "-g5", "-g500", "-g1000", "-g1001", "-g99999",
      "-g0", "-g", "-gx", "-g12x", "-G7 -g8", "-g 5", "-g00012", "-g1", "-g2 -a", "-dp", "-dP", "-dF", "-df", "-dM",
      "-dm", "-dpfm", "-dxyz", "-d", "-d p", "-D,P", "-dQ", "-dqp", "-p name", "-pname", "-P", "-p", "-p ",
      "-pTHISNAMEISWAYTOOLONGFORTHEBUFFER xx", "-p  spaced", "-pexactly15chars!", "-pexactly14chars", "-q", "-",
      "/", "- ", "-- -", "-@", "-[", "-`", "-y", "-z", "-Z", "-E", "-e", "-I", "-i", "-\xe9\x61", "-\x80", "a b c d",
      "!game -c", "\x01g -c", "-a-b", "-t-t", "/w", "/x /h", "/T", "game.hst -a -w -t -g12 -dpf -p nm -c -x -h", "-a -a -a", "\t-a",
      "-a\t-w", "game\t-a", "-aw", "-twx", "-hhh"};
  /* -- start-up outcomes (4:0x0000, 1:0x0000 and 1:0x0af8 are the real, decompiled code) -- */
  /* No window class of the game exists yet. A previous instance (hPrevInstance 1) does not call 4:0x0000, so
   * "starsframe" is missing and 1:0x0af8 fails: the fatal box. */
  wm("", 1, 3);
  wm("-w game", 1, 3);
  /* The first instance (hPrevInstance 0) runs the real 4:0x0000, which registers its eleven classes (DIFF
   * restores the arena after each run, so every run starts again from an empty class table and the class table
   * is compared). 1:0x0000 then fails (no bitmaps in the module), so no main window is created; the first-instance
   * runs where the whole start-up succeeds (main window, real FRAMEWNDPROC) follow below. */
  load_module_without_bitmaps();
  wm("", 0, 3);                       /* 4:0x0000 returns 1; the game start-up (1:0x0000) failed: its own box, then silent exit */
  wm("game.hst", 0, 3);
  wm("-a", 1, 3);
  load_module();
  /* A first instance whose start-up succeeds: the real 4:0x0000 registers the real FRAMEWNDPROC, the real 1:0x0af8
   * creates the main window and the message loop dispatches the queued message 0x464 to the real FRAMEWNDPROC
   * (the by-name DispatchMessage lowers the stack pointer by 8 like the import thunk, stars-n7l). */
  {
    /* The start-up allocates about 2.3 MB of global blocks (the game's arrays), more than the 2 MB that DIFF
     * snapshots beyond the highest block. A free hole of 3 MB below a pinned block puts them inside the snapshot. */
    uint16_t hole = GlobalAlloc(0x2, 0x300000), pin = GlobalAlloc(0x2, 0x100);
    CHECK(hole != 0 && pin != 0);
    CHECK(GlobalLock(pin) != 0);
    CHECK_EQ(GlobalFree(hole), 0);
    wm("", 0, 3);
    wm("game.hst", 0, 3);
    CHECK_EQ(GlobalUnlock(pin), 0);
    CHECK_EQ(GlobalFree(pin), 0);
  }
  /* A previous instance has registered the main window class (a test procedure that answers 1 to every message,
   * in place of FRAMEWNDPROC). The same name makes the first RegisterClass of the real 4:0x0000 fail. */
  register_frame_class();
  wm("", 0, 3);                       /* class registration failed (the first RegisterClass): the fatal box */
  wm("game.hst", 0, 3);
  wm("", 1, 3);                       /* all good, a previous instance: 4:0x0000 is not called */
  wm("", 1, 3);

  /* -- the command line (hPrevInstance 1: 4:0x0000 is not called) -- */
  for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) wm(cmds[i], 1, 0);
  /* every option letter, upper and lower case, alone, and every other letter in the table's range */
  for (unsigned c = 0x21; c < 0x80; c++) {
    char one[8] = {'-', (char)c, 0};
    wm(one, 1, 0);
  }
  for (unsigned c = 0x21; c < 0x80; c++) {
    char one[8] = {'/', (char)c, 'x', 0};
    wm(one, 1, 0);
  }

  /* -- the message loop -- */
  DS_W(0x2806) = g_wm_win;
  DS_W(0x150) = g_wm_other;
  PostMessage(g_wm_win, 0x100, 0x41, 0x001E0001u); /* A down: TranslateMessage queues WM_CHAR */
  PostMessage(g_wm_win, 0x101, 0x41, 0xC01E0001u);
  PostMessage(g_wm_win, 0x102, 0x78, 0x00000001u);
  PostMessage(g_wm_win, 0x200, 0, 0x00050005u);
  wm("", 1, 7);
  PostMessage(g_wm_win, 0x102, 0x2b, 1);           /* '+' goes to DS:0x150 */
  PostMessage(g_wm_win, 0x102, 0x2d, 1);
  PostMessage(g_wm_win, 0x102, 0x76, 1);
  PostMessage(g_wm_win, 0x102, 0x56, 1);
  PostMessage(g_wm_win, 0x102, 0x2c, 1);
  wm("", 1, 0);
  DS_W(0x150) = 0;                                  /* with no target only 'v' and 'V' are caught */
  PostMessage(g_wm_win, 0x102, 0x2b, 1);
  PostMessage(g_wm_win, 0x102, 0x76, 1);
  wm("", 1, 1);
  DS_W(0x158) = g_wm_other;                         /* the edit window keeps the character when it has the focus */
  DS_W(0x150) = g_wm_other;
  PostMessage(g_wm_win, 0x102, 0x2b, 1);
  PostMessage(g_wm_win, 0x102, 0x76, 1);
  wm("", 1, 1);
  SetFocus(g_wm_other);
  DS_W(0x59e6) = g_wm_other;
  PostMessage(g_wm_win, 0x102, 0x2b, 1);
  PostMessage(g_wm_win, 0x102, 0x76, 1);
  wm("", 1, 1);
  DS_W(0x158) = 0;
  DS_W(0x59e6) = 0;
  DS_W(0x150) = g_wm_other;
  /* key down/up filters: ESC with a window to cancel swallows the message */
  DS_W(0xbbe) = g_wm_other;
  PostMessage(g_wm_win, 0x100, 0x1b, 1);
  PostMessage(g_wm_win, 0x101, 0x1b, 1);
  PostMessage(g_wm_win, 0x100, 0x10, 1);            /* SHIFT: WM_SETCURSOR to DS:0x150 if under the cursor */
  PostMessage(g_wm_win, 0x100, 0x28, 1);            /* down arrow */
  wm("", 1, 2);
  DS_W(0xbbe) = 0;
  /* key up of ESC / Enter in a child of the window DS:0x15A reaches 13:0x0fb8 (real code: the zoom box, 13:0x0fb8) */
  {
    uint16_t par = mkwin(WS_OVERLAPPED, 0, 0, 50, 50, 0, 0);
    uint16_t kid = mkwin(WS_CHILD, 0, 0, 20, 20, par, 0x22);
    SetFocus(kid);
    DS_W(0x15a) = par;
    PostMessage(g_wm_win, 0x101, 0x1b, 1);
    PostMessage(g_wm_win, 0x101, 0x0d, 1);
    PostMessage(g_wm_win, 0x101, 0x41, 1);
    wm("", 1, 1);
    DS_W(0x15a) = 0;
    SetFocus(g_wm_other);
    (void)DestroyWindow(par);
  }
  DS_W(0x158) = g_wm_other;
  PostMessage(g_wm_win, 0x100, 0x28, 1);            /* forwarded to DS:0x158 and swallowed */
  PostMessage(g_wm_win, 0x100, 0x24, 1);
  wm("", 1, 2);
  DS_W(0x158) = 0;
  /* accelerator tables: DS:0x2666 (resource 0x74) in the normal case, DS:0x4C72 (0x438) when DS:0x312 is set */
  uint16_t ha = LoadAccelerators(0x0118, 0x74), hb = LoadAccelerators(0x0118, 0x438);
  CHECK(ha != 0 && hb != 0);
  UAccelEntry ea[8], eb[8];
  unsigned na = user_accel_entries(ha, ea, 8), nb = user_accel_entries(hb, eb, 8);
  CHECK(na > 0 && nb > 0);
  DS_W(0x2666) = ha;
  DS_W(0x4c72) = hb;
  for (unsigned mode = 0; mode < 2; mode++) {
    DS_W(0x312) = (uint16_t)mode;
    for (unsigned w = 0; w < 3; w++) {
      DS_W(0x2806) = w == 2 ? g_wm_icon : g_wm_win;
      for (unsigned k = 0; k < (mode ? nb : na) && k < 3; k++) {
        const UAccelEntry *e = mode ? &eb[k] : &ea[k];
        PostMessage(DS_W(0x2806), (e->fVirt & 1) ? 0x100 : 0x102, e->key, 1);
      }
      PostMessage(DS_W(0x2806), 0x100, 0x41, 1);
      PostMessage(DS_W(0x2806), 0x102, 0x2b, 1);
      PostMessage(DS_W(0x2806), 0x0111, 0x0123, 0);
      g_wm_show = (w == 2) ? 2 : 1;                  /* w == 2: the main window is iconic: no accelerator translation */
      wm("", (uint16_t)(w + 1), (int16_t)(mode + w)); /* a previous instance: the real 4:0x0000 is not called */
      g_wm_show = 1;
    }
  }
  DS_W(0x312) = 0;
  DS_W(0x2806) = g_wm_win;
  /* exit codes */
  static const int16_t codes[] = {0, 1, 0x7FFF, -1, -32768, 255};
  for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) wm("", 1, codes[i]);
  /* WM_QUIT absent: the runtime ends the queue itself (nothing to wait for) */
  DIFF(3, 0x0000, FAR_PASCAL, 0x0118, 1, HI(fstr("")), LO(fstr("")), 1);
  drain_queue();
  DS_W(0x2666) = 0;
  DS_W(0x4c72) = 0;
  DS_W(0x150) = 0;
  (void)DestroyWindow(g_wm_win);
  (void)DestroyWindow(g_wm_other);
  (void)DestroyWindow(g_wm_icon);
}

int main(void) {
  g_only = getenv("SEG03_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  register_test_class();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* 1:0x08a4 (via 3:0x054c) uses LocalAlloc */
  stubs_init();
  stub_install(24, 0x0e24, 16, 8); /* the pop-up menu of the toolbar (13:0x09ae items 8, 0xd, 0xf, 0x10) */
  stub_install(31, 0x61b0, 17, 0);
  stub_install(10, 0x606a, 4, 1);
  stub_install(7, 0x153e, 5, 2);
  stub_install(9, 0x07e0, 6, 2);
  stub_install(32, 0x5418, 7, 5);
  stub_install(8, 0x86cc, 11, 2);
  stub_install(8, 0x8d94, 14, 2);
  stub_install(8, 0xb28a, 15, 1);
  stub_watch(15, true, 0x2560, 16); /* the -p name is copied to DS:0x2560 before 8:0xb28a is called */
  if (want("0c7a")) t_0c7a();
  if (want("0ec2")) t_0ec2();
  if (want("0f3a")) t_0f3a();
  if (want("04a2")) t_04a2();
  if (want("03e0")) t_03e0();
  if (want("054c")) t_054c();
  if (want("0f90")) t_0f90();
  if (want("0ca0")) t_0ca0();
  if (want("0766")) t_0766();
  if (want("0000")) t_0000();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
