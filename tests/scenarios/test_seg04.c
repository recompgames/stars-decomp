/* Diff tests for decompiled segment 4 (src/seg04.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 4 is the main window of the game and its helpers:
 *   4:0x0000 class registration, 4:0x02ca caption, 4:0x0528 FRAMEWNDPROC, 4:0x143a, 4:0x18b0 marker
 *   squares, 4:0x197a / 0x1e5e selection, 4:0x1af2 / 0x1c78 serial coder, 4:0x1f40 ..., the title window
 *   4:0x60f6 and the host dialogs 4:0x467c / 0x4cec (see the comment above each test).
 * Every scenario builds its state by writing DGROUP and a few far blocks directly (and windows with
 * the runtime); DIFF then snapshots it and compares the original with the C version (return
 * registers, all memory outside the callee's own frame, the ordered Win16 API call log, which here also
 * holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See the notes of each function in
 * docs/functions.csv for which memory a watch hashes and which it does not.
 *
 * SEG04_ONLY=tag[,tag] runs only the named parts: a mutation check of one function does not need the
 * whole run. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/commdlg_thunks.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/vfs.h"
#include "runtime/user.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}
static uint32_t fstr(const char *s) {
  uint32_t p = gblock((uint32_t)strlen(s) + 1);
  memcpy(LIN(p), s, strlen(s) + 1);
  return p;
}

/* Windows. The class procedure answers every message with 1 (MOV AX,1; XOR DX,DX; RETF 10). */
static uint16_t g_class_ctr;
static uint16_t g_test_proc_sel;
static void register_test_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  g_test_proc_sel = sel;
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG04TEST");
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG04TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy,
                            parent, id, 0x1234, 0);
  CHECK(h != 0);
  g_class_ctr++;
  return h;
}

/* A top-level window with a menu bar of three pop-ups (the main window of the game has STARSMENU). */
static uint16_t menu_window(void) {
  uint16_t w = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t bar = CreateMenu();
  CHECK(bar != 0);
  for (unsigned i = 0; i < 6; i++) {
    uint16_t pop = CreatePopupMenu();
    CHECK(pop != 0);
    for (unsigned k = 0; k < 3; k++) CHECK(AppendMenu(pop, 0, 0x80 + 8 * i + k, fstr("item")) != 0);
    {
      /* the fourth item opens a nested pop-up (4:0x379a counts the items of GetSubMenu(.., 3)) */
      uint16_t sub = CreatePopupMenu();
      CHECK(sub != 0);
      for (unsigned k = 0; k < 2; k++) CHECK(AppendMenu(sub, 0, 0x300 + 8 * i + k, fstr("sub")) != 0);
      CHECK(AppendMenu(pop, MF_POPUP, sub, fstr("nested")) != 0);
    }
    CHECK(AppendMenu(bar, MF_POPUP, pop, fstr("pop")) != 0);
  }
  CHECK(SetMenu(w, bar) != 0);
  return w;
}
/* The class names 4:0x0000 registers (DGROUP offsets of the strings). Registering the same name again fails,
 * which is how t_0000 makes 4:0x0000 stop at each class. */
static const uint16_t g_class_names[11] = {0x196, 0x1c2, 0x1a2, 0x1ac, 0x202, 0x1b6, 0x1d0, 0x20a, 0x1dc, 0x1ea, 0x1f6};
/* The procedure of the report window class DS:0x1f6: WM_DESTROY clears DS:0x177e (as the real one does), then answers 1.
 * PUSH BP; MOV BP,SP; CMP [BP+0xC],2; JNE +6; MOV WORD [0x177E],0; MOV AX,1; XOR DX,DX; POP BP; RETF 10 */
static uint16_t g_report_proc_sel;
static uint16_t reg_game_class(uint16_t name_off) {
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  if (name_off == 0x1f6 && g_report_proc_sel == 0) {
    static const uint8_t code[] = {0x55, 0x89, 0xE5, 0x83, 0x7E, 0x0C, 0x02, 0x75, 0x06, 0xC7, 0x06, 0x7E, 0x17,
                                   0x00, 0x00, 0xB8, 0x01, 0x00, 0x31, 0xD2, 0x5D, 0xCA, 0x0A, 0x00};
    g_report_proc_sel = harness_alloc_code(code, sizeof code);
    CHECK(g_report_proc_sel != 0);
  }
  w->lpfnWndProc = MK_FP(name_off == 0x1f6 ? g_report_proc_sel : g_test_proc_sel, 0);
  w->lpszClassName = MK_FP(MEM_SEL_DGROUP, name_off);
  w->hInstance = 0x1234;
  return RegisterClass(wc);
}

/* ---- test doubles: logged thunks (rule j2) ---- */

/* A double replaces the first 5 bytes of a callee in the code segment image with JMP FAR to an import
 * thunk of the module "STUBS" (CDECL: the caller pops the arguments), so the same bytes serve the ORIGINAL
 * run and the C version's CALL_ORIG. Every call is an entry of the API log (module, id, the argument
 * words as pushed, the return value), in order with the real API calls and the other doubles. What the
 * log does not show is the memory a pointer argument points to: for a watched stub the handler hashes
 * the memory it names at every call into a log in the arena, which DIFF compares as arena memory.
 * The return value is the next value queued for the id (stub_push), else the default (stub_def). A
 * stub with `outw` >= 0 also writes the word `stub_out(id, v)` to the DGROUP/SS offset in argument word
 * `outw` (the callee's output parameter), the same in both runs. The queue index lives in the arena and
 * is rewound with every snapshot restore. The functions under test are never doubled. */
#define STUB_BLKSZ 0x8000
#define STUB_DEF 0x10   /* default return (ax, dx) of id: 4 bytes each */
#define STUB_OUT 0x110  /* output word of id */
#define STUB_QLEN 0x200 /* queue length of id (word) */
#define STUB_QIDX 0x280 /* queue index of id (word) */
#define STUB_Q 0x400    /* queue of id: 64 entries of 4 bytes (ax, dx) */
#define STUB_QMAX 64
#define STUB_LOG0 0x4000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs; /* argument words */
  int8_t outw;   /* argument word index of a near pointer that gets the output word, -1 none */
} StubCfg;
enum {
  S_9_7C26, S_9_2ED8, S_10_26B2, S_7_14A8, S_31_61B0,
  S_14_2992, S_14_0508, S_14_2CAA, S_14_31C2, S_8_B90E, S_14_305A, S_14_3100, S_14_0370, S_8_B1EC, S_8_8D94,
  S_8_B8CC, S_8_B8F6, S_14_54EA, S_14_2F40, S_22_0000, S_14_52BC, S_28_0000, S_11_4C62,
  S_9_3AA8, S_4_506C, S_4_1F40, S_4_379A, S_26_0000, S_9_3082, S_10_1AEC, S_11_5142, S_31_5CC2, S_9_78A0, S_9_649A,
  S_7_3EF4, S_15_39D4,
  S_32_67A8, S_32_5ADA, S_32_5954, S_10_606A, S_9_80DA, S_9_7EEA, S_9_641C, S_14_2F88, S_14_30D8, S_14_35B0, S_9_7D1E,
  S_31_64F8, S_31_5F14, S_15_6AB2, S_25_0000, S_9_431C, S_9_07E0, S_11_0AB0, S_29_01C2,
  S_NSTUB
};
static const StubCfg g_stubs[S_NSTUB] = {
    [S_9_7C26] = {"9:7c26", 9, 0x7c26, 2, 1},
    [S_9_2ED8] = {"9:2ed8", 9, 0x2ed8, 2, -1},
    [S_10_26B2] = {"10:26b2", 10, 0x26b2, 2, -1},
    [S_7_14A8] = {"7:14a8", 7, 0x14a8, 1, -1},
    [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, -1},
    [S_14_2992] = {"14:2992", 14, 0x2992, 0, -1},
    [S_14_0508] = {"14:0508", 14, 0x0508, 2, -1},
    [S_14_2CAA] = {"14:2caa", 14, 0x2caa, 3, -1},
    [S_14_31C2] = {"14:31c2", 14, 0x31c2, 0, -1},
    [S_8_B90E] = {"8:b90e", 8, 0xb90e, 1, -1},
    [S_14_305A] = {"14:305a", 14, 0x305a, 0, -1},
    [S_14_3100] = {"14:3100", 14, 0x3100, 2, -1},
    [S_14_0370] = {"14:0370", 14, 0x0370, 2, -1},
    [S_8_B1EC] = {"8:b1ec", 8, 0xb1ec, 0, -1},
    [S_8_8D94] = {"8:8d94", 8, 0x8d94, 2, -1},
    [S_8_B8CC] = {"8:b8cc", 8, 0xb8cc, 0, -1},
    [S_8_B8F6] = {"8:b8f6", 8, 0xb8f6, 0, -1},
    [S_14_54EA] = {"14:54ea", 14, 0x54ea, 4, -1},
    [S_14_2F40] = {"14:2f40", 14, 0x2f40, 1, -1},
    [S_22_0000] = {"22:0000", 22, 0x0000, 0, -1},
    [S_14_52BC] = {"14:52bc", 14, 0x52bc, 2, -1},
    [S_28_0000] = {"28:0000", 28, 0x0000, 3, -1},
    [S_11_4C62] = {"11:4c62", 11, 0x4c62, 1, -1},
    [S_9_3AA8] = {"9:3aa8", 9, 0x3aa8, 1, -1},
    [S_4_506C] = {"4:506c", 4, 0x506c, 0, -1},
    [S_4_1F40] = {"4:1f40", 4, 0x1f40, 2, -1},
    [S_4_379A] = {"4:379a", 4, 0x379a, 1, -1},
    /* Rule j2 justification: 26:0x0000 is decompiled (src/seg26.c, diffed in tests/scenarios/test_seg26.c), but it stays a double
     * here. Only the WM_CHAR scenario reaches it (keys 0x71 / 0x51) and its world is random state; the real function builds
     * the production list from the planet copy at DS:0x4c08 and queue pointer DS:0x4c3c and opens a modal dialog, so the two
     * runs then differ in the heap block it allocates (measured: 4:0x0528 DIFF fails on a heap block with the double removed).
     * Blind spot: what 26:0x0000 does is not checked here, only that 4:0x0528 calls it with argument 0. */
    [S_26_0000] = {"26:0000", 26, 0x0000, 1, -1},
    [S_9_3082] = {"9:3082", 9, 0x3082, 2, -1},
    [S_10_1AEC] = {"10:1aec", 10, 0x1aec, 4, -1},
    [S_11_5142] = {"11:5142", 11, 0x5142, 3, -1},
    [S_31_5CC2] = {"31:5cc2", 31, 0x5cc2, 1, -1},
    [S_9_78A0] = {"9:78a0", 9, 0x78a0, 1, -1},
    [S_9_649A] = {"9:649a", 9, 0x649a, 0, -1},
    [S_7_3EF4] = {"7:3ef4", 7, 0x3ef4, 2, -1},
    [S_15_39D4] = {"15:39d4", 15, 0x39d4, 1, -1},
    [S_32_67A8] = {"32:67a8", 32, 0x67a8, 0, -1},
    [S_32_5ADA] = {"32:5ada", 32, 0x5ada, 0, -1},
    [S_32_5954] = {"32:5954", 32, 0x5954, 0, -1},
    [S_10_606A] = {"10:606a", 10, 0x606a, 1, -1},
    [S_9_80DA] = {"9:80da", 9, 0x80da, 1, -1},
    [S_9_7EEA] = {"9:7eea", 9, 0x7eea, 1, -1},
    [S_9_641C] = {"9:641c", 9, 0x641c, 1, -1},
    [S_14_2F88] = {"14:2f88", 14, 0x2f88, 3, -1},
    [S_14_30D8] = {"14:30d8", 14, 0x30d8, 1, -1},
    [S_14_35B0] = {"14:35b0", 14, 0x35b0, 3, -1},
    [S_9_7D1E] = {"9:7d1e", 9, 0x7d1e, 2, -1},
    [S_31_64F8] = {"31:64f8", 31, 0x64f8, 0, -1},
    [S_31_5F14] = {"31:5f14", 31, 0x5f14, 1, -1},
    [S_15_6AB2] = {"15:6ab2", 15, 0x6ab2, 2, -1},
    [S_25_0000] = {"25:0000", 25, 0x0000, 2, -1},
    [S_9_431C] = {"9:431c", 9, 0x431c, 5, -1},
    [S_9_07E0] = {"9:07e0", 9, 0x07e0, 2, -1},
    [S_11_0AB0] = {"11:0ab0", 11, 0x0ab0, 1, -1},
    [S_29_01C2] = {"29:01c2", 29, 0x01c2, 0, -1},
};
static uint8_t g_stub_orig[S_NSTUB][5]; /* the bytes a double replaced, to take it away again */

/* Scripted side effects of a double: at its call number `call` (0 based; -1 every call) the handler writes
 * `len` bytes to DGROUP offset `a` (kind 0) or to the memory a near (SS) pointer argument word `a` points
 * to, at byte `b` (kind 1). They are the same in the ORIGINAL run and the C run (host data, consulted by
 * the handler; the call counter is in the arena). The double stands for a callee whose real writes the
 * scenario needs to see: they are not the real callee's writes. */
typedef struct StubEff {
  int call;
  int kind;
  uint16_t a, b;
  uint16_t len;
  uint8_t data[0xc0];
} StubEff;
#define MAXEFF 16
static StubEff g_eff[S_NSTUB][MAXEFF];
static unsigned g_neff[S_NSTUB];
#define STUB_NCALL 0x300 /* call counter of id (word) */

static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  if (qidx < qlen) {
    memcpy(&c->ret, blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * 4 + (size_t)qidx * 4, 4);
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
  if (cfg->outw >= 0) {
    uint16_t p = hcall_word(c, (unsigned)cfg->outw);
    memcpy(LIN(MK_FP(c->regs->ss, p)), blk + STUB_OUT + 2 * c->ordinal, 2);
  }
  uint16_t ncall;
  memcpy(&ncall, blk + STUB_NCALL + 2 * c->ordinal, 2);
  for (unsigned k = 0; k < g_neff[c->ordinal]; k++) {
    const StubEff *e = &g_eff[c->ordinal][k];
    if (e->call != -1 && e->call != (int)ncall) continue;
    if (e->kind == 3) {
      int y, mo, d, h, mi, sc; /* the runtime clock moves on by a seconds */
      rt_clock_get(&y, &mo, &d, &h, &mi, &sc);
      rt_clock_set(y, mo, d, h, mi, sc + e->a);
    } else if (e->kind == 2) {
      rt_input_key(e->a, 0, e->b, 0); /* a key event: virtual key a, down b */
    } else if (e->kind == 0) {
      memcpy(LINN(e->a), e->data, e->len);
    } else {
      uint16_t p = hcall_word(c, e->a);
      memcpy(LIN(MK_FP(c->regs->ss, (uint16_t)(p + e->b))), e->data, e->len);
    }
  }
  ncall++;
  memcpy(blk + STUB_NCALL + 2 * c->ordinal, &ncall, 2);
}
static void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
static void stub_out(unsigned id, uint16_t v) { memcpy((uint8_t *)LIN(g_stub_blk) + STUB_OUT + 2 * id, &v, 2); }
/* Queues one return (dx:ax) for the id. */
__attribute__((unused)) static void stub_push(unsigned id, uint16_t ax, uint16_t dx) {
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t n;
  memcpy(&n, blk + STUB_QLEN + 2 * id, 2);
  CHECK(n < STUB_QMAX);
  if (n >= STUB_QMAX) return;
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy(blk + STUB_Q + (size_t)id * STUB_QMAX * 4 + (size_t)n * 4, &v, 4);
  n++;
  memcpy(blk + STUB_QLEN + 2 * id, &n, 2);
}
/* Forgets the queues, the defaults and the output words of every double. */
static void stubs_clear_all(void) {
  harness_log_clear();
  uint8_t *blk = LIN(g_stub_blk);
  for (unsigned id = 0; id < S_NSTUB; id++) {
    memset(blk + STUB_QLEN + 2 * id, 0, 2);
    memset(blk + STUB_QIDX + 2 * id, 0, 2);
    memset(blk + STUB_NCALL + 2 * id, 0, 2);
    stub_def(id, 0, 0);
    stub_out(id, 0);
    g_neff[id] = 0;
  }
}
static void stub_eff_ds(unsigned id, int call, uint16_t off, const void *data, uint16_t len) {
  CHECK(g_neff[id] < MAXEFF && len <= sizeof g_eff[0][0].data);
  StubEff *e = &g_eff[id][g_neff[id]++];
  e->call = call;
  e->kind = 0;
  e->a = off;
  e->b = 0;
  e->len = len;
  memcpy(e->data, data, len);
}
static void stub_eff_arg(unsigned id, int call, uint16_t argw, uint16_t off, const void *data, uint16_t len) {
  CHECK(g_neff[id] < MAXEFF && len <= sizeof g_eff[0][0].data);
  StubEff *e = &g_eff[id][g_neff[id]++];
  e->call = call;
  e->kind = 1;
  e->a = argw;
  e->b = off;
  e->len = len;
  memcpy(e->data, data, len);
}
/* At the call `call` of the double a key event is queued (virtual key vk, down 0 or 1), as the user releasing a key. */
static void stub_eff_key(unsigned id, int call, uint16_t vk, uint16_t down) {
  CHECK(g_neff[id] < MAXEFF);
  StubEff *e = &g_eff[id][g_neff[id]++];
  e->call = call;
  e->kind = 2;
  e->a = vk;
  e->b = down;
  e->len = 0;
}
__attribute__((unused)) static void stub_uninstall(unsigned id) {
  const StubCfg *cfg = &g_stubs[id];
  memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), g_stub_orig[id], 5);
}
/* At the call `call` of the double the runtime clock (TimerCount, GetTickCount) advances by secs seconds. */
static void stub_eff_clock(unsigned id, int call, uint16_t secs) {
  CHECK(g_neff[id] < MAXEFF);
  StubEff *e = &g_eff[id][g_neff[id]++];
  e->call = call;
  e->kind = 3;
  e->a = secs;
  e->b = 0;
  e->len = 0;
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(g_stub_orig[i], LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), sizeof c);
    memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
  }
  stubs_clear_all();
  /* 4:0x1f40 is decompiled and tested: it is never a double */
  stub_uninstall(S_4_1F40);
  /* 4:0x379a is decompiled and tested too */
  stub_uninstall(S_4_379A);
  /* 4:0x506c is decompiled and tested too */
  stub_uninstall(S_4_506C);
}
/* Takes a double away (the callee has been decompiled and is tested for real from here on). */

/* The player rows (0xc0 bytes at DS:0x5a64) get random bytes; DS:0x78 is the player count. */
static void players(unsigned n) {
  for (unsigned k = 0; k < 16; k++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * k));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
  }
  DS_W(0x78) = (uint16_t)n;
}

/* 7:0x0b84 copies the NUL-terminated names at row +0x80 and +0xa0 of a player row into a 0x32 byte buffer:
 * the random rows of players() would overflow it, so the scenarios that reach it give the rows short names. */
static void sane_names(void) {
  for (unsigned k = 0; k < 16; k++) {
    for (unsigned f = 0x80; f <= 0xa0; f += 0x20) {
      uint8_t *nm = LINN((uint16_t)(0x5a64 + 0xc0 * k + f));
      unsigned len = rn(4) == 0 ? 0 : rn(16);
      memset(nm, 0, 0x20);
      for (unsigned i = 0; i < len; i++) nm[i] = (uint8_t)(rn(5) == 0 ? ' ' : 'a' + rn(26));
      if (rn(3) == 0) nm[len] = (uint8_t)(rn(2) ? 's' : 'e'), nm[len + 1] = 0;
    }
  }
}

/* ---- 4:0x0000: register the eleven window classes ---- */
/* A class that is already registered makes RegisterClass fail, so pre-registering the class names from the
 * last to the first (the class table lives in the arena, DIFF rolls the callee's own registrations back but
 * not these) makes 4:0x0000 stop at each of the eleven classes in turn. */
static void t_0000(void) {
  DS_W(0x54f4) = 0x4321;
  DIFF(4, 0x0000, FAR_CDECL);
  for (int k = 10; k >= 0; k--) {
    CHECK(reg_game_class(g_class_names[k]) != 0);
    DIFF(4, 0x0000, FAR_CDECL);
  }
}

/* ---- 4:0x4db8: a bare RETF ---- */
static void t_4db8(void) { DIFF(4, 0x4db8, FAR_CDECL); }

/* ---- 4:0x3b30: the pop-up menu of the main window ---- */
static void t_3b30(void) {
  uint16_t w = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t bar = CreateMenu();
  CHECK(bar != 0);
  uint16_t pop[3];
  for (unsigned i = 0; i < 3; i++) {
    pop[i] = CreatePopupMenu();
    CHECK(pop[i] != 0);
    CHECK(AppendMenu(pop[i], 0, 100 + i, fstr("item")) != 0);
    CHECK(AppendMenu(bar, MF_POPUP, pop[i], fstr("pop")) != 0);
  }
  CHECK(SetMenu(w, bar) != 0);
  uint16_t normal = mkwin(WS_OVERLAPPED | WS_VISIBLE, 40, 40, 100, 100, 0, 0);
  uint16_t zoomed = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MAXIMIZE, 50, 50, 100, 100, 0, 0);
  CHECK(IsZoomed(zoomed) != 0);
  CHECK(IsZoomed(normal) == 0);
  const uint16_t held[] = {0, normal, zoomed, 0x7777};
  for (unsigned h = 0; h < sizeof held / 2; h++) {
    DS_W(0x152) = held[h];
    for (int p = -2; p <= 4; p++) DIFF(4, 0x3b30, FAR_CDECL, w, (uint16_t)p);
    DIFF(4, 0x3b30, FAR_CDECL, 0, 0);
    DIFF(4, 0x3b30, FAR_CDECL, normal, 1);
  }
  DIFF(4, 0x3b30, FAR_CDECL, w, 0x7fff);
  DIFF(4, 0x3b30, FAR_CDECL, w, 0x8000);
}

/* ---- 4:0x4fd6 and 4:0x500c: the window placement and its text ---- */
#define SCR 0x3000 /* a scratch area in DGROUP the scenarios write */
static void t_placement(void) {
  uint16_t normal = mkwin(WS_OVERLAPPED | WS_VISIBLE, 33, 44, 123, 77, 0, 0);
  uint16_t big = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MAXIMIZE, 5, 6, 300, 200, 0, 0);
  uint16_t icon = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MINIMIZE, 70, 80, 150, 90, 0, 0);
  uint16_t hid = mkwin(WS_OVERLAPPED, 0x7ff0, 0x7ff0, 0x400, 0x400, 0, 0);
  const uint16_t ws[] = {normal, big, icon, hid, 0, 0x4321};
  for (unsigned i = 0; i < sizeof ws / 2; i++) {
    for (unsigned k = 0; k < 8; k++) DS_W(SCR + 2 * k) = (uint16_t)rnd();
    DIFF(4, 0x4fd6, FAR_CDECL, ws[i], SCR);
    DIFF(4, 0x500c, FAR_CDECL, 0x1111, ws[i]);
    DIFF(4, 0x500c, FAR_CDECL, 0, ws[i]);
  }
}

/* ---- 4:0x18b0: the three marker squares ---- */
static void t_18b0(void) {
  static const int16_t cols[] = {0, 5, 100, -8, -9, 0x7ff9, 0x7ff8, -0x7ffc};
  for (unsigned it = 0; it < 4000; it++) {
    g_rng = 0x18b00001u + 0x9e3779b9u * (it + 1);
    int16_t c0 = cols[rn(8)], c1 = cols[rn(8)], c2 = cols[rn(8)];
    DS_W(0x264e) = (uint16_t)c0;
    DS_W(0x2650) = (uint16_t)c1;
    DS_W(0x2652) = (uint16_t)c2;
    static const uint16_t modes[] = {0, 1, 2, 3, 0xffff, 0x8000, 0x7fff, 4};
    DS_W(0x255e) = modes[rn(8)];
    DS_W(0x2670) = 0x1111;
    DS_W(0x53b2) = 0x2222;
    DS_W(0x2940) = 0x3333;
    /* points on and around the edges of the squares */
    static const int16_t d[] = {-9, -8, -7, -1, 0, 1, 7, 8, 9};
    int16_t x = (int16_t)((rn(2) ? c0 : (rn(2) ? c1 : c2)) + d[rn(9)]);
    int16_t y = (int16_t)((rn(2) ? c1 : c2) + d[rn(9)]);
    if (rn(8) == 0) x = (int16_t)rnd();
    if (rn(8) == 0) y = (int16_t)rnd();
    DS_W(SCR) = 0xeeee;
    DIFF(4, 0x18b0, FAR_CDECL, (uint16_t)x, (uint16_t)y, rn(3) ? SCR : 0);
  }
}

/* ---- 4:0x4580 / 4:0x451c: the per-player turn-file states ---- */
static void state_world(unsigned n) {
  players(n);
  for (unsigned k = 0; k < 16; k++) {
    uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * k));
    row[7] = (uint8_t)(rn(3) == 0 ? (row[7] | 2) : (row[7] & ~2));
    row[0x54] = (uint8_t)(rn(3) == 0 ? (row[0x54] | 1) : (row[0x54] & ~1));
    DS_W(0x6666 + 2 * k) = (uint16_t)rn(7);
  }
  DS_B(0x790) = (uint8_t)rnd();
  DS_B(0x791) = (uint8_t)rnd();
  DS_B(0x792) = (uint8_t)rnd();
  ds_w32(0x192, rn(3) == 0 ? 0 : rnd());
}
static void t_4580(void) {
  static const uint16_t codes[] = {0, 0x1c, 0x1d, 1, 2, 3, 0x1b, 0x1e, 0xffff, 0x7fff};
  for (unsigned it = 0; it < 600; it++) {
    g_rng = 0x45800001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned n = rn(5) == 0 ? rn(17) : 1 + rn(6);
    state_world(n);
    /* 9:0x7c26 answers 0 (the file is readable) or not, and writes the code; per player */
    for (unsigned k = 0; k < n; k++) {
      stub_push(S_9_7C26, rn(5) == 0 ? 1 : 0, 0);
    }
    stub_out(S_9_7C26, codes[rn(10)]);
    DS_W(0x14c) = (uint16_t)rn(4);
    DIFF(4, 0x4580, FAR_CDECL);
    if (rn(2)) DIFF(4, 0x451c, FAR_CDECL);
  }
  /* a negative or zero player count runs no loop; the last player changes nothing when equal */
  stubs_clear_all();
  for (int n = -2; n <= 0; n++) {
    state_world(3);
    DS_W(0x78) = (uint16_t)n;
    DIFF(4, 0x4580, FAR_CDECL);
    DIFF(4, 0x451c, FAR_CDECL);
  }
}

/* ---- the planet and fleet tables 4:0x1e5e and 4:0x197a search ---- */
/* Planets (DS:0xb6, count DS:0x4d96, 0x38 bytes: id word, owner word) and fleets (DS:0xba, a table of far
 * pointers, count DS:0x553a, records with the id at +0 and the owner at +2). 7:0x01e4 (planet by id) runs
 * as ORIGINAL code in both runs: it reads DS:0x7a, DS:0x4d96 and DS:0xb6. */
#define MAXP 8
static uint32_t g_pl, g_ft, g_fr[MAXP];
static void pf_init(void) {
  g_pl = gblock(0x38 * MAXP);
  g_ft = gblock(4 * MAXP);
  for (unsigned i = 0; i < MAXP; i++) g_fr[i] = gblock(0x20);
}
static void pf_world(unsigned np, unsigned nf, uint16_t cur, bool binary) {
  DS_W(0x14c) = cur;
  uint8_t *pl = LIN(g_pl);
  uint16_t id = (uint16_t)rn(3);
  for (unsigned i = 0; i < np; i++) {
    uint16_t ow = rn(3) == 0 ? cur : (uint16_t)(cur + 1 + rn(3));
    memcpy(pl + 0x38 * i, &id, 2);
    memcpy(pl + 0x38 * i + 2, &ow, 2);
    id = (uint16_t)(id + 1 + rn(3));
  }
  ds_w32(0xb6, g_pl);
  DS_W(0x4d96) = (uint16_t)np;
  DS_W(0x7a) = binary ? (uint16_t)(np + rn(4)) : (uint16_t)np;
  uint32_t *ft = LIN(g_ft);
  for (unsigned i = 0; i < nf; i++) {
    uint8_t *fr = LIN(g_fr[i]);
    uint16_t fid = (uint16_t)(100 + i), ow = rn(3) == 0 ? cur : (uint16_t)(cur + 1 + rn(3));
    memcpy(fr, &fid, 2);
    memcpy(fr + 2, &ow, 2);
    ft[i] = rn(6) == 0 ? 0 : g_fr[i];
  }
  ds_w32(0xba, g_ft);
  DS_W(0x553a) = (uint16_t)nf;
  /* the player's row +8 word: the home planet id (an existing one, a missing one, 0xffff) */
  uint16_t home = np && rn(3) ? *(uint16_t *)(pl + 0x38 * rn(np)) : (uint16_t)(rn(2) ? 0xffff : rn(40));
  DS_W((uint16_t)(0x5a6c + 0xc0 * cur)) = home;
}
static void t_1e5e(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x1e5e0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t cur = (uint16_t)rn(4);
    pf_world(rn(MAXP + 1), rn(MAXP + 1), cur, rn(2));
    DIFF(4, 0x1e5e, FAR_CDECL);
  }
  /* a negative fleet count and a negative planet count */
  pf_world(3, 3, 1, false);
  DS_W(0x553a) = 0x8000;
  DIFF(4, 0x1e5e, FAR_CDECL);
  DS_W(0x553a) = 0xffff;
  DIFF(4, 0x1e5e, FAR_CDECL);
}

/* The state the real 6:0x6ef4 and 6:0x6a68 read (they run as ORIGINAL code in both runs): the message buffer
 * DS:0xaac/0xaae (a zeroed block of 0x1000 bytes, offset 0) holding DS:0xab4 records (a
 * word with the id, the subject word, then one byte per value), its length DS:0xab0, the filter bits
 * DS:0x5508, the message-window controls DS:0x59e6, 0x53e0, 0x4da4, 0x54fe, 0x4da0, 0x4d9e and 0x4da2 (child
 * windows of the test window) and the "busy" word DS:0x15fe (non-zero ends 6:0x6a68 at once). Call it after DS:0xab4 is set. */
static void msg_state(const uint16_t *ctl) {
  static uint32_t buf;
  if (!buf) {
    buf = gblock(0x1000);
    CHECK_EQ(LO(buf), 0);
  }
  uint8_t *b = (uint8_t *)LIN(buf);
  memset(b, 0, 0x1000);
  unsigned n = DS_W(0xab4) < 5 ? DS_W(0xab4) : 5, len = 0;
  for (unsigned i = 0; i < n; i++) {
    /* a record as 6:0x7668 walks it: the id (below 0x181; the values of the id are the signed byte
     * cs:0xe74 + id), the subject, then one byte per value (no word flags); at most 7 values fit its 0x12 byte
     * local. */
    uint16_t id = (uint16_t)rn(0x181), subj = (uint16_t)rnd();
    int nv = (int8_t)*(uint8_t *)LIN(MK_FP(SEG_SEL(6), (uint16_t)(0x0e74 + id)));
    if (nv > 7) nv = 7;
    memcpy(b + len, &id, 2);
    memcpy(b + len + 2, &subj, 2);
    len += 4;
    for (int k = 0; k < nv; k++) b[len++] = (uint8_t)rnd();
  }
  DS_W(0xaac) = LO(buf);
  DS_W(0xaae) = HI(buf);
  DS_W(0xab0) = (uint16_t)len;
  static const uint16_t ctl_off[7] = {0x59e6, 0x53e0, 0x4da4, 0x54fe, 0x4da0, 0x4d9e, 0x4da2};
  for (unsigned i = 0; i < 7; i++) DS_W(ctl_off[i]) = ctl[i];
  for (unsigned i = 0; i < 0x31; i++) DS_B((uint16_t)(0x5508 + i)) = (uint8_t)rnd();
  DS_W(0x15fe) = rn(6) == 0 ? 1 : 0;
}

/* ---- 4:0x197a: keep the selection record in step ---- */
static void t_197a(void) {
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t ctl[7];
  for (unsigned i = 0; i < 7; i++) ctl[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 50, 20, win, (uint16_t)(0x100 + i));
  static const uint16_t kinds[] = {0x40, 0x20, 0x00, 0x60, 0x80, 0x100, 0x1e0, 0x1c0, 0x01, 0x21, 0x41};
  for (unsigned it = 0; it < 900; it++) {
    g_rng = 0x197a0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    uint16_t cur = (uint16_t)rn(3);
    pf_world(rn(MAXP + 1), rn(MAXP + 1), cur, rn(2));
    DS_W(0x158) = rn(5) ? win : 0;
    DS_W(0x2520) = rn(5) ? cur : (uint16_t)(cur + 1);
    ds_w32(0x70, 0x12345678);
    ds_w32(0x2522, rn(5) ? 0x12345678u : 0x12345679u);
    DS_W(0x251a) = (uint16_t)((rnd() & 0xfe1f) | kinds[rn(11)]);
    uint16_t pid = DS_W((uint16_t)(0x5a6c + 0xc0 * cur));
    DS_W(0x251e) = rn(3) == 0 ? (uint16_t)(rn(2) ? 0xffff : rn(40)) : pid;
    DS_W(0x82) = 77;
    DS_W(0x251c) = rn(4) ? 77 : 78;
    DS_W(0x2528) = (uint16_t)((int)rn(9) - 2);
    DS_W(0xab4) = (uint16_t)rn(5);
    DS_W(0xac4) = (uint16_t)rn(5);
    msg_state(ctl);
    DS_W(0xab6) = 0x5555;
    DS_B(0x791) = (uint8_t)rnd();
    DS_B(0x53f0) = (uint8_t)rnd();
    stub_def(S_7_14A8, 0, 0);
    if (rn(2)) stub_push(S_7_14A8, 0x1234, 0x5678);
    DIFF(4, 0x197a, FAR_CDECL);
  }
  /* DS:0x2528 on both sides of 0, and the two sums on both sides of DS:0x2528 - 1 */
  for (int v = -1; v <= 3; v++) {
    for (unsigned a = 0; a < 3; a++) {
      g_rng = 0x197a1001u + (unsigned)(v + 1) * 16 + a;
      stubs_clear_all();
      pf_world(2, 2, 0, false);
      DS_W(0x158) = win;
      DS_W(0x2520) = 0;
      ds_w32(0x70, 7);
      ds_w32(0x2522, 7);
      DS_W(0x251a) = 0x0000;
      DS_W(0x251e) = 0;
      DS_W(0x82) = 5;
      DS_W(0x251c) = 5;
      DS_W(0x2528) = (uint16_t)v;
      DS_W(0xab4) = (uint16_t)a;
      DS_W(0xac4) = 1;
      msg_state(ctl);
      DS_B(0x791) = (uint8_t)(a == 1 ? 0x08 : 0x00);
      DIFF(4, 0x197a, FAR_CDECL);
    }
  }
}

/* ---- 4:0x1af2 and 4:0x1c78: the 28-character code coder (synthetic keys and data only) ---- */
/* 14:0x2bb4 (does a key lie in the accepted ranges) is pure and runs as ORIGINAL code; the keys below are
 * built from its definition: q * 36^4 + r with q one of the accepted quotients and r a remainder in range. */
static uint16_t key_ok(uint32_t key) { return (uint16_t)rt_call_orig(14, 0x2bb4, FAR_CDECL, 2, LO(key), HI(key)); }
static uint32_t make_key(void) {
  static const uint16_t qs[] = {2, 4, 6, 7, 8, 9, 10, 11, 12, 15, 18, 22};
  for (;;) {
    uint32_t k = qs[rn(12)] * 1679616u + 100u + rn(0x15ff00u);
    if (key_ok(k)) return k;
  }
}
#define CK_KEY (SCR + 0x00)
#define CK_DATA (SCR + 0x10)
#define CK_TEXT (SCR + 0x40)
static void t_coder(void) {
  /* 1af2 encodes anything: keys including 0 and 0xffffffff, data of all zero, all one, random */
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x1af20001u + 0x9e3779b9u * (it + 1);
    uint32_t key = it == 0 ? 0 : it == 1 ? 0xffffffffu : it < 40 ? make_key() : rnd();
    for (unsigned i = 0; i < 11; i++) DS_B(CK_DATA + i) = it % 5 == 0 ? 0 : it % 5 == 1 ? 0xff : (uint8_t)rnd();
    memset(&DS_B(CK_TEXT), 0xee, 40);
    DIFF(4, 0x1af2, FAR_CDECL, LO(key), HI(key), CK_DATA, CK_TEXT);
  }
  /* 1c78 reads what 1af2 writes: encode with the ORIGINAL 1af2, then decode (and damage the text) */
  static const char alpha[] = "AZaz09-*@[`{/: \x7f\x80\xff!";
  for (unsigned it = 0; it < 260; it++) {
    g_rng = 0x1c780001u + 0x9e3779b9u * (it + 1);
    uint32_t key = make_key();
    for (unsigned i = 0; i < 11; i++) DS_B(CK_DATA + i) = (uint8_t)rnd();
    rt_call_orig(4, 0x1af2, FAR_CDECL, 4, LO(key), HI(key), CK_DATA, CK_TEXT);
    CHECK_EQ(strlen((char *)&DS_B(CK_TEXT)), 28);
    unsigned dmg = it % 4; /* 0: none, 1: one character, 2: two, 3: a random character of the alphabet */
    for (unsigned d = 0; d < dmg; d++) {
      unsigned at = rn(28);
      DS_B(CK_TEXT + at) = (uint8_t)(d == 2 || dmg == 3 ? alpha[rn(sizeof alpha - 1)] : 'A' + rn(26));
    }
    if (it % 9 == 8) ds_w32(CK_KEY, 0xdeadbeef);
    for (unsigned i = 0; i < 11; i++) DS_B(CK_DATA + i) = (uint8_t)(0xa0 + i); /* cleared by the callee */
    ds_w32(CK_KEY, 0x11223344);
    DIFF(4, 0x1c78, FAR_CDECL, CK_KEY, CK_DATA, CK_TEXT);
  }
  /* arbitrary text: almost always rejected by 14:0x2bb4 */
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x1c781001u + 0x9e3779b9u * (it + 1);
    for (unsigned i = 0; i < 28; i++) DS_B(CK_TEXT + i) = it % 3 ? alpha[rn(sizeof alpha - 1)] : (uint8_t)rnd();
    DS_B(CK_TEXT + 28) = 0;
    DIFF(4, 0x1c78, FAR_CDECL, CK_KEY, CK_DATA, CK_TEXT);
  }
}

/* ---- 4:0x39f8: the turn-file loop over the players ---- */
/* Doubles: 14:0x2992 and 14:0x0508 (host state write and file load; no effect, 0 returned), 14:0x2caa (is
 * the player's file readable: a queued answer per player), 14:0x31c2 and 8:0xb90e (nothing). 17:0x0000
 * (decompiled, so it runs for real in both runs) finds 14:0x0508 failing at once and returns. Blind
 * spots: the real files, the progress bar, 17:0x0000's deeper path (tested in test_seg17.c). */
static void t_39f8(void) {
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x39f80001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    unsigned n = rn(6) == 0 ? rn(17) : 1 + rn(6);
    players(n);
    for (unsigned k = 0; k < 16; k++) {
      uint8_t *row = LINN((uint16_t)(0x5a64 + 0xc0 * k));
      row[7] = (uint8_t)(rn(2) ? (row[7] | 2) : (row[7] & ~2));
    }
    for (unsigned k = 0; k < n; k++) stub_push(S_14_2CAA, rn(2), 0);
    DS_B(0x790) = (uint8_t)rnd();
    DS_B(0x791) = (uint8_t)(rn(5) == 0 ? (rnd() | 4) : (rnd() & ~4));
    DS_W(0x76e) = (uint16_t)rn(3);
    DS_W(0x14c) = (uint16_t)rn(4);
    DIFF(4, 0x39f8, FAR_CDECL);
  }
  stubs_clear_all();
  for (int n = -1; n <= 0; n++) {
    players(3);
    DS_W(0x78) = (uint16_t)n;
    DS_B(0x790) = 0x08;
    DS_B(0x791) = 0;
    DIFF(4, 0x39f8, FAR_CDECL);
    DS_B(0x790) = 0x18;
    DIFF(4, 0x39f8, FAR_CDECL);
  }
}

/* ---- 4:0x3ecc: is this a good game file? ---- */
/* Doubles: 14:0x3100 (open) and 14:0x305a (read a block) do nothing real: the scenario sets the three blocks
 * 3ecc looks at with scripted writes to DS (header block at call 0, player block at call 1, end block
 * at call 2); 14:0x0370 writes the player row into the caller's buffer through its first argument;
 * 8:0xb1ec, 14:0x31c2 and 8:0x8d94 return the queued value / nothing. 34:0x0800 (setjmp) runs for real;
 * the throw back to it cannot be diffed (see the note in docs/functions.csv). Blind spots: the real
 * block decoding, the file contents. */
/* The doubles' scripts for one run of 4:0x3ecc (see the comment above): the three blocks it reads, the player row
 * 14:0x0370 gives, 8:0xb1ec's answer. good = 1 makes all of them acceptable. */
static void script_3ecc(int flavor) {
  bool good = flavor != 0;
  stubs_clear_all();
  strcpy((char *)&DS_B(0x2560), "Player Name");
  memcpy(&DS_B(0x5866), "text from the failing call", 27);
  /* the version word DS:0x4dba (its high byte DS:0x4dbb must have 2 in the high nibble) is accepted in
   * [0x620, 0xa40) after the mask 0xfe0 */
  static const uint16_t edge[][2] = {{6, 0x20}, {6, 0x1f}, {6, 0x00}, {0xa, 0x3f}, {0xa, 0x40}, {0xa, 0x20}, {9, 0xff}, {0, 0}};
  unsigned z = good ? 7 : rn(2) ? 6 + rn(5) : rn(16);
  unsigned lo = good ? 0x40 : rnd() & 0xff;
  if (!good && rn(4) == 0) {
    unsigned e = rn(8);
    z = edge[e][0];
    lo = edge[e][1];
  }
  uint8_t hi = (uint8_t)((!good && rn(8) == 0 ? rnd() & 0xf0 : 0x20) | z);
  uint16_t ver = (uint16_t)(hi << 8 | lo);
  uint16_t hdr = (uint16_t)((!good && rn(10) == 0 ? (rnd() & 0x3f) : 8) << 10 | 0x10);
  uint8_t b5 = flavor == 2 ? 6 : !good && rn(8) == 0 ? (uint8_t)rnd() : 5; /* flavor 2: 3ecc says "not a game file" (0) */
  stub_eff_ds(S_14_305A, 0, 0x2910, &hdr, 2);
  stub_eff_ds(S_14_305A, 0, 0x4dba, &ver, 2);
  stub_eff_ds(S_14_305A, 0, 0x4dbb, &hi, 1);
  stub_eff_ds(S_14_305A, 0, 0x4dc0, &b5, 1);
  /* call 1: the player block; call 2: the end block */
  uint16_t h1 = (uint16_t)((!good && rn(8) == 0 ? (rnd() & 0x3f) : 6) << 10 | 0xc0);
  uint16_t h2 = (uint16_t)((!good && rn(8) == 0 ? (rnd() & 0x3f) : 0) << 10);
  stub_eff_ds(S_14_305A, 1, 0x2910, &h1, 2);
  stub_eff_ds(S_14_305A, 2, 0x2910, &h2, 2);
  uint8_t row[0xc0];
  for (unsigned i = 0; i < sizeof row; i++) row[i] = (uint8_t)rnd();
  uint32_t dw = good ? (rnd() | 1) : rn(3) == 0 ? 0 : rnd() | 1;
  memcpy(row + 0xc, &dw, 4);
  stub_eff_arg(S_14_0370, -1, 0, 0, row, sizeof row);
  stub_push(S_8_B1EC, good ? 1 : (uint16_t)rn(2), 0);
  DS_W(0x76e) = (uint16_t)rn(3);
  DS_W(0x6e) = 0x1234;
  ds_w32(0x31c, rnd());
  DS_W(0x772) = 0x4321;
  for (unsigned i = 0; i < 0x20; i++) DS_B(0x1434 + i) = 0xee;
  for (unsigned i = 0; i < 0x20; i++) DS_B(0x1444 + i) = 0xdd;
}
static void t_3ecc(void) {
  const char *name = "GAME.HST";
  for (unsigned it = 0; it < 800; it++) {
    g_rng = 0x3ecc0001u + 0x9e3779b9u * (it + 1);
    memset(&DS_B(0x3000), 0, 32);
    memcpy(&DS_B(0x3000), name, strlen(name) + 1);
    script_3ecc(0);
    DIFF(4, 0x3ecc, FAR_CDECL, 0x3000, (uint16_t)rn(2));
  }
}

/* ---- 4:0x5d98: lay out the child windows ---- */
/* The windows are real runtime windows of the test class; MoveWindow, IsIconic, GetMenu, GetSubMenu and
 * CheckMenuItem run for real in both runs (4:0x3b30 too, it is decompiled). */
static void t_5d98(void) {
  uint16_t win = menu_window();
  uint16_t icon = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MINIMIZE, 70, 80, 150, 90, 0, 0);
  uint16_t kid[5];
  for (unsigned i = 0; i < 5; i++) kid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, win, (uint16_t)(1 + i));
  for (unsigned it = 0; it < 700; it++) {
    g_rng = 0x5d980001u + 0x9e3779b9u * (it + 1);
    static const uint16_t modes[] = {0, 1, 2, 3, 4, 0xffff, 0x8000, 0x7fff};
    DS_W(0x255e) = modes[rn(8)];
    DS_W(0x2806) = rn(20) == 0 ? 0 : rn(20) == 0 ? icon : win;
    DS_W(0x264a) = (uint16_t)(rn(6) == 0 ? rn(300) : 200 + rn(900));
    DS_W(0x264c) = (uint16_t)(rn(6) == 0 ? rn(300) : 100 + rn(700));
    DS_W(0x267a) = (uint16_t)(rn(8) == 0 ? rnd() : 4 + rn(40));
    DS_W(0x2654) = (uint16_t)rn(500);
    DS_W(0x265a) = (uint16_t)rn(500);
    DS_W(0x2656) = (uint16_t)(rn(2) ? rn(100) : rn(400));
    DS_W(0x2658) = (uint16_t)(rn(2) ? rn(100) : rn(400));
    DS_W(0x265c) = (uint16_t)(rn(2) ? rn(100) : rn(400));
    DS_W(0x265e) = (uint16_t)(rn(2) ? rn(100) : rn(400));
    DS_W(0x264e) = 0x1111;
    DS_W(0x2650) = 0x2222;
    DS_W(0x2652) = 0x3333;
    DS_W(0x0150) = rn(8) ? kid[0] : 0;
    DS_W(0x0154) = kid[1];
    DS_W(0x0156) = kid[2];
    DS_W(0x0158) = kid[3];
    DS_W(0x015a) = kid[4];
    DS_W(0x0152) = rn(2) ? 0 : win;
    DS_B(0x0793) = (uint8_t)rnd();
    DIFF(4, 0x5d98, FAR_CDECL);
  }
}

/* ---- 4:0x02ca: the caption of the main window and its child windows ---- */
/* Double: 11:0x4c62 (a map window refresh; nothing, argument logged: the child window handle). 7:0x0b84 (the
 * player's name), 34:0x0352 / 0x05a0 (string copy and upper-case), _wsprintf and 4:0x5d98 run for real in
 * both runs. Blind spot: what 11:0x4c62 would do to the window. */
static void t_02ca(void) {
  uint16_t win = menu_window();
  uint16_t kid[5];
  for (unsigned i = 0; i < 5; i++) kid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, win, (uint16_t)(1 + i));
  static const char *names[] = {"C:\\GAMES\\STARS\\game.hst", "game.hst", "", "a:b", "\\", "VERYLONGNAME12.HST",
                                "d:dir\\x", "abcdefgh", "abcdefghi", "\\\\", "x:"};
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x02ca0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    players(1 + rn(5));
    sane_names();
    DS_W(0x14c) = rn(4) == 0 ? 0xffff : (uint16_t)rn(16);
    memset(&DS_B(0x5764), 0, 0x40);
    strcpy((char *)&DS_B(0x5764), names[rn(11)]);
    DS_W(0x2806) = win;
    DS_W(0x255e) = (uint16_t)rn(4);
    DS_W(0x264a) = 600;
    DS_W(0x264c) = 400;
    DS_W(0x267a) = 12;
    DS_W(0x0150) = rn(2) ? kid[0] : 0;
    DS_W(0x0154) = rn(2) ? kid[1] : 0;
    DS_W(0x0156) = rn(2) ? kid[2] : 0;
    DS_W(0x0158) = rn(2) ? kid[3] : 0;
    DS_W(0x015a) = rn(2) ? kid[4] : 0;
    DS_W(0x27ea) = 0;
    DS_W(0x27ec) = 0;
    DIFF(4, 0x02ca, FAR_CDECL);
  }
}

/* ---- 4:0x4dba: the timer procedure of the main window ---- */
/* Doubles: 14:0x2f40 (may the next player's file be read: queued answer), 14:0x2992 / 14:0x0508 (host state
 * write and file load: nothing, 0x0508 answers from its queue), 8:0x8d94 (message box), 8:0xb8cc, 8:0xb8f6,
 * 22:0x0000; 9:0x7c26 (4:0x4580's file check) and 14:0x2caa, 14:0x31c2, 8:0xb90e (4:0x39f8's). A scripted
 * write of the 8:0xb8f6 double sets DS:0x251a bit 3 (quit) at one of its first calls, which ends the loop
 * "count 0: run the turn again". 4:0x02ca, 4:0x4580, 4:0x39f8, 17:0x0000 and 8:0x8df4 run for real in both runs.
 * Blind spots: what the doubles would do to the game state (the real 22:0x0000 and 8:0xb8f6 are not run). */
static void t_4dba(void) {
  uint16_t win = menu_window();
  uint16_t pop = mkwin(WS_POPUP | WS_VISIBLE, 20, 20, 100, 100, win, 0);
  uint16_t lone = mkwin(WS_OVERLAPPED | WS_VISIBLE, 200, 20, 100, 100, 0, 0);
  const uint16_t wins[] = {win, lone, pop};
  uint16_t kid[5];
  for (unsigned i = 0; i < 5; i++) kid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, win, (uint16_t)(1 + i));
  static const uint16_t codes[] = {0, 0x1c, 0x1d, 1, 2, 3, 0x1b, 0x1e};
  static const uint16_t states[] = {0xe, 0xe, 0xe, 0xf, 0xd, 0x14, 0, 0x10, 0xffff};
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x4dba0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    state_world(1 + rn(5));
    sane_names();
    strcpy((char *)&DS_B(SCR + 0x200), "G%d");
    DS_W(0x190) = SCR + 0x200;
    DS_W(0x3c0) = rn(15) == 0 ? 1 : 0;
    DS_W(0x76e) = (uint16_t)rn(3);
    DS_W(0x164) = states[rn(9)];
    DS_W(0x162) = (uint16_t)rnd();
    /* the "no current player" value 0xffff only where 4:0x39f8 (so 17:0x0000, which indexes with DS:0x14c) is
     * not reached: states 0xe and 0xf */
    DS_W(0x14c) = (rn(5) == 0 && (DS_W(0x164) == 0xe || DS_W(0x164) == 0xf)) ? 0xffff : (uint16_t)rn(4);
    ds_w32(0x53e2, rnd());
    DS_W(0x2806) = wins[rn(3)];
    DS_W(0x366) = (uint16_t)rnd();
    DS_B(0x251a) = (uint8_t)(rnd() & ~8u);
    DS_B(0x793) = (uint8_t)rnd();
    DS_W(0x255e) = (uint16_t)rn(4);
    DS_W(0x264a) = 600;
    DS_W(0x264c) = 400;
    DS_W(0x267a) = 12;
    /* the child windows exist: 4:0x02ca then invalidates them. Its second CreateWindow takes its width and
     * height from two uninitialised stack words (BP-4, BP-2), which hold what earlier calls left there (return
     * addresses, thunk frames) and so differ between the original and the C version once 4:0x02ca is not
     * the first call: that CreateWindow is only tested in t_02ca, where the stack below is clean. */
    for (unsigned i = 0; i < 5; i++) DS_W(i == 0 ? 0x150 : i == 1 ? 0x154 : i == 2 ? 0x156 : i == 3 ? 0x158 : 0x15a) = kid[i];
    memset(&DS_B(0x5764), 0, 0x40);
    strcpy((char *)&DS_B(0x5764), "C:\\GAMES\\game.hst");
    stub_push(S_14_2F40, rn(4) != 0, 0);
    /* only 4:0x4dba's own call of 14:0x0508 succeeds: 17:0x0000 (run for real by 4:0x39f8) then stops at
     * once, as in t_39f8 */
    stub_push(S_14_0508, rn(4) != 0, 0);
    for (unsigned k = 0; k < 40; k++) stub_push(S_14_2CAA, rn(2), 0);
    stub_def(S_9_7C26, rn(5) == 0, 0);
    stub_out(S_9_7C26, codes[rn(8)]);
    static const uint16_t quit[] = {8, 0x0108, 0x18};
    uint16_t qv = quit[rn(3)];
    stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
    if (DIFF(4, 0x4dba, FAR_PASCAL, wins[rn(3)], 0x0113, 0x000d, 0x1234, 0x5678) != DT_PASS)
      fprintf(stderr, "  4dba iteration %u: state %x, [2806] %x, [14c] %x\n", it, DS_W(0x164), DS_W(0x2806), DS_W(0x14c));
  }
}

/* ---- 4:0x4090: the host-mode dialog loop ---- */
/* The dialog procedure 4:0x467c runs as ORIGINAL code in both runs (DialogBox calls it through the runtime),
 * answered by queued key presses: Return presses the default button, Escape cancels. Doubles: 14:0x54ea,
 * 14:0x2992, 8:0xb8cc, 8:0xb8f6, 22:0x0000 and those of t_4dba / t_39f8. Blind spot: the keys decide the
 * dialog result, so the results the keys cannot give (-1 only by a bad instance handle) are reached with
 * DS:0x54f4 = 0. */
/* Releases Shift and Control and drops every queued input event (outside DIFF: what a scenario queued stays in the
 * arena after its DIFF). */
static void input_reset(void) {
  uint32_t m = gblock(16);
  rt_input_key(VK_SHIFT, 0x2a, 0, 0);
  rt_input_key(VK_CONTROL, 0x1d, 0, 0);
  for (unsigned i = 0; i < 2000 && PeekMessage(m, 0, 0, 0, 1); i++) {
  }
  (void)GetAsyncKeyState(VK_SHIFT);
  (void)GetAsyncKeyState(VK_CONTROL);
  harness_log_clear(); /* the API calls of the setup are logged too; DIFF copies the log, so it must not grow without end */
}
static void key_tap(uint16_t vk, uint16_t scan) {
  rt_input_key(vk, scan, 1, 0);
  rt_input_key(vk, scan, 0, 0);
}
static void t_4090(void) {
  uint16_t win = menu_window();
  uint16_t kid[5];
  for (unsigned i = 0; i < 5; i++) kid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, win, (uint16_t)(1 + i));
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x40900001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    state_world(1 + rn(4));
    sane_names();
    DS_W(0x2806) = win;
    DS_W(0x54f4) = rn(6) == 0 ? 0 : MEM_SEL_DGROUP;
    DS_W(0x14c) = (uint16_t)rn(3);
    DS_B(0x251a) = (uint8_t)(rnd() & 0xf3);
    if (rn(4) == 0) DS_B(0x251a) |= 4;
    if (rn(4) == 0) DS_B(0x251a) |= 8;
    DS_B(0x793) = (uint8_t)rnd();
    DS_B(0x794) = (uint8_t)rnd();
    DS_W(0x378) = (uint16_t)rn(3);
    ds_w32(0x53e2, rnd());
    DS_W(0x0312) = 0;
    DS_W(0x0314) = 0;
    DS_W(0x76e) = 0;
    DS_W(0x3c0) = 0;
    for (unsigned i = 0; i < 5; i++) DS_W(i == 0 ? 0x150 : i == 1 ? 0x154 : i == 2 ? 0x156 : i == 3 ? 0x158 : 0x15a) = kid[i];
    memset(&DS_B(0x5764), 0, 0x40);
    strcpy((char *)&DS_B(0x5764), "C:\\GAMES\\game.hst");
    stub_push(S_14_2F40, rn(4) != 0, 0);
    stub_def(S_9_7C26, rn(5) == 0, 0);
    stub_out(S_9_7C26, 0);
    static const uint16_t quit[] = {8, 0x0108, 0x18};
    uint16_t qv = quit[rn(3)];
    stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
    /* Return presses the default button (the dialog ends with 1 and is shown again); Escape cancels (0) and ends
     * the loop. With Shift (10 turns) or Control (100) held when Return comes, the dialog asks the countdown
     * question in a message box first (Return = Yes, N = No) and DS:0x378 counts down in 4:0x4090 while neither
     * key is held; both together make 1000. Every script ends with Escape. */
    {
      const uint16_t SH = VK_SHIFT, CT = VK_CONTROL;
      input_reset();
      switch (rn(8)) {
        case 0: key_tap(VK_RETURN, 0x1c); break;
        case 1: key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c); break;
        /* the key is held when the dialog reads it and released by the double 22:0x0000 (the first pass of
         * the countdown loop), so 4:0x4090 counts down: 10 turns for Shift, 100 for Control */
        case 2: rt_input_key(SH, 0x2a, 1, 0); key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c);
                stub_eff_key(S_22_0000, 0, SH, 0); break;
        case 3: rt_input_key(CT, 0x1d, 1, 0); key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c);
                stub_eff_key(S_22_0000, 0, CT, 0); break;
        /* held to the end: 4:0x4090 sees the key down and stops counting */
        case 4: rt_input_key(SH, 0x2a, 1, 0); key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c); break;
        case 5: rt_input_key(CT, 0x1d, 1, 0); key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c); break;
        /* the question answered with N */
        case 6: rt_input_key(CT, 0x1d, 1, 0); key_tap(VK_RETURN, 0x1c); key_tap('N', 0x31); break;
        default:
          if (it % 4 == 0) { /* both keys: 1000 turns */
            rt_input_key(SH, 0x2a, 1, 0); rt_input_key(CT, 0x1d, 1, 0);
            key_tap(VK_RETURN, 0x1c); key_tap(VK_RETURN, 0x1c);
            stub_eff_key(S_22_0000, 0, SH, 0); stub_eff_key(S_22_0000, 0, CT, 0);
          }
          break;
      }
      key_tap(VK_ESCAPE, 0x01);
    }
    if (DIFF(4, 0x4090, FAR_CDECL) != DT_PASS) fprintf(stderr, "  4090 iteration %u\n", it);
  }
}

/* The runtime creates the root of the file dialog (a block in the arena) at the first use of GetOpenFileName or
 * PrintDlg and remembers its offset in a host variable: created inside DIFF it would be rolled back with the arena
 * while the variable stays. One call outside any DIFF creates it for good. */
static void commdlg_warm(void) {
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 5, 5, 40, 40, 0, 0);
  uint32_t ofn = gblock(sizeof(OPENFILENAME16));
  OPENFILENAME16 *o = LIN(ofn);
  o->lStructSize = sizeof *o;
  o->hwndOwner = win;
  static const char flt[] = "Game\0*.hst\0";
  o->lpstrFilter = gblock(sizeof flt + 1);
  memcpy(LIN(o->lpstrFilter), flt, sizeof flt);
  o->nFilterIndex = 1;
  o->lpstrFile = gblock(0x100);
  o->nMaxFile = 0x100;
  o->lpstrFileTitle = gblock(0x100);
  o->nMaxFileTitle = 0x100;
  o->Flags = 0x1804;
  rt_input_key(VK_ESCAPE, 0x01, 1, 0);
  rt_input_key(VK_ESCAPE, 0x01, 0, 0);
  CHECK_EQ(GetOpenFileName(ofn), 0);
  input_reset();
  (void)DestroyWindow(win);
}

/* ---- 4:0x3b66: open a game file ---- */
/* Doubles: those of t_3ecc (3b66 calls 4:0x3ecc, which is decompiled and runs for real, with its doubles scripted
 * by script_3ecc), 14:0x2992, 14:0x0508 (answers once), 14:0x52bc (a name for DS:0x5866), 28:0x0000, and for
 * the host-mode dialog 4:0x4090 reaches (decompiled, run for real) 14:0x54ea, 8:0xb8cc, 8:0xb8f6, 22:0x0000.
 * The file dialog is the runtime's, answered by typed keys over the VFS (C:\STARS holds GAME.HST). 34:0x0578,
 * 34:0x0352, 8:0x8dc0, 4:0x02ca and 4:0x1e5e run for real. Blind spots: the real file loading behind the doubles;
 * the CreateWindow of 4:0x02ca (see t_4dba). */
static const char *g_typed;
static const char *const b66_names[] = {"C:\\STARS\\GAME.HST", "C:\\STARS\\GAME", "NODIR.HST", "", "C:\\STARS\\SUB\\X.X1", "D:\\G.HST", "a.b\\c"};
static void b66_world(uint16_t win, const uint16_t kid[5]) {
  input_reset();
  uint16_t cur = rn(3) == 0 ? 0xffff : (uint16_t)rn(3);
  pf_world(rn(MAXP + 1), rn(MAXP + 1), cur == 0xffff ? 0 : cur, rn(2));
  DS_W(0x14c) = cur;
  sane_names();
  script_3ecc(rn(3) == 0 ? 0 : rn(2) ? 1 : 2);
  DS_W(0x2806) = win;
  DS_W(0x54f4) = MEM_SEL_DGROUP;
  memset(&DS_B(0x5764), 0, 0x40);
  strcpy((char *)&DS_B(0x5764), b66_names[rn(7)]);
  memset(&DS_B(0x212), 0, 0x20);
  if (rn(2)) strcpy((char *)&DS_B(0x212), "C:\\STARS");
  uint16_t f = (uint16_t)(rnd() & 0xfe1f);
  f = (uint16_t)((f & ~1u) | (rn(2) ? 1u : 0u));
  f = (uint16_t)((f & ~0x1e0u) | (rn(2) ? 0x20u * rn(9) : 0u));
  if (rn(3) == 0) f |= 8;
  DS_W(0x251a) = f;
  ds_w32(0x872, rn(2) ? 0 : rnd());
  DS_W(0x312) = rn(2) ? kid[0] : 0;
  DS_W(0x314) = (uint16_t)rn(2);
  ds_w32(0x70, 0x12345678);
  ds_w32(0x2522, rn(3) ? 0x12345678u : 0x12345679u);
  DS_W(0x82) = 77;
  DS_W(0x251c) = rn(2) ? 77 : 76;
  DS_B(0x794) = (uint8_t)rnd();
  DS_W(0x4d96) = (uint16_t)rn(2);
  DS_W(0x264a) = 600;
  DS_W(0x264c) = 400;
  DS_W(0x267a) = 12;
  DS_W(0x255e) = (uint16_t)rn(4);
  for (unsigned i = 0; i < 5; i++) DS_W(i == 0 ? 0x150 : i == 1 ? 0x154 : i == 2 ? 0x156 : i == 3 ? 0x158 : 0x15a) = kid[i];
  DS_B(0x793) = (uint8_t)rnd();
  DS_B(0x790) = (uint8_t)rnd();
  DS_B(0x792) = (uint8_t)rnd();
  DS_W(0x378) = 0;
  ds_w32(0x53e2, rnd());
  stub_push(S_14_0508, rn(3) != 0, 0);
  stub_eff_ds(S_14_52BC, -1, 0x5866, "NEWNAME.HST", 12);
  stub_def(S_9_7C26, rn(5) == 0, 0);
  static const uint16_t quit[] = {8, 0x0108, 0x18};
  uint16_t qv = quit[rn(3)];
  stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
  /* the file dialog (not when the current name is used): a typed name and Return, or Escape */
  if (rn(3) != 0) {
    static const char *typed[] = {"GAME.HST", "OTHER.HST", "NOSUCH.HST", "GAME"};
    g_typed = typed[rn(4)];
    for (const char *q = g_typed; *q; q++) key_tap((uint16_t)(*q == '.' ? 0xBE : *q), 0x10);
    key_tap(VK_RETURN, 0x1c);
  } else {
    g_typed = "<Esc>";
    key_tap(VK_ESCAPE, 0x01);
  }
  key_tap(VK_ESCAPE, 0x01);
  key_tap(VK_ESCAPE, 0x01);
  key_tap(VK_ESCAPE, 0x01);
}
static void t_3b66(void) {
  uint16_t win = menu_window();
  uint16_t kid[5];
  for (unsigned i = 0; i < 5; i++) kid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, win, (uint16_t)(1 + i));
  CHECK_EQ(vfs_put("C:\\STARS\\GAME.HST", "h", 1), 0);
  CHECK_EQ(vfs_put("C:\\STARS\\OTHER.HST", "h", 1), 0);
  for (unsigned it = 0; it < 300; it++) {
    g_rng = 0x3b660001u + 0x9e3779b9u * (it + 1);
    b66_world(win, kid);
    uint16_t hw = rn(8) == 0 ? 0 : win;
    uint16_t mode = (uint16_t)rn(2);
    if (DIFF(4, 0x3b66, FAR_CDECL, hw, mode) != DT_PASS)
      fprintf(stderr, "  3b66 iteration %u: 251a=%x mode=%u hw=%x [14c]=%x name=%s typed=%s\n", it, DS_W(0x251a), mode, hw, DS_W(0x14c), (char *)&DS_B(0x5764), g_typed);
  }
}

/* ---- 4:0x0528 FRAMEWNDPROC: the main window procedure ---- */
/* Doubles (logged thunks, rule j2): 9:0x3aa8, 4:0x506c, 4:0x1f40, 4:0x379a, 26:0x0000, 9:0x3082, 10:0x1aec,
 * 11:0x5142, 31:0x5cc2, 9:0x78a0, 9:0x649a, 7:0x3ef4, 15:0x39d4, 14:0x2f40 / 0x2992 / 0x0508 / 0x54ea, 8:0x8d94,
 * 31:0x61b0, 22:0x0000, 9:0x2ed8, 10:0x26b2, 8:0xb8f6, 8:0xb8cc and those of 4:0x39f8 / 4:0x4580 (9:0x7c26,
 * 14:0x2caa, 14:0x31c2, 8:0xb90e). The windows are real windows of the test class (answering 1 to every message);
 * 1:0x08a4, 1:0x0bb4, 3:0x054c, 4:0x18b0, 4:0x143a, 4:0x5d98, 4:0x02ca, 4:0x451c, 4:0x39f8, 4:0x4090 and the
 * mouse loop 8:0xa2c6 (PeekMessage on the queued mouse events) run for real in both runs. */
static uint16_t g_fwin, g_fkid[5], g_fpop, g_ficon;
static uint16_t g_fpal;
static void fw_windows(void) {
  static bool made; /* the menus are a limited resource; the windows stay (DIFF rolls back what a run does to them) */
  if (made) return;
  made = true;
  {
    uint32_t lp = gblock(4 + 4 * 4);
    uint8_t *q = LIN(lp);
    const uint16_t ver = 0x300, n = 4;
    memcpy(q, &ver, 2);
    memcpy(q + 2, &n, 2);
    for (unsigned i = 0; i < 4; i++) {
      q[4 + 4 * i] = (uint8_t)(40 * i + 7);
      q[5 + 4 * i] = (uint8_t)(90 * i + 3);
      q[6 + 4 * i] = (uint8_t)(20 * i);
      q[7 + 4 * i] = 0;
    }
    g_fpal = CreatePalette(lp);
    CHECK(g_fpal != 0);
  }
  g_fwin = menu_window();
  for (unsigned i = 0; i < 5; i++) g_fkid[i] = mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 20, 20, g_fwin, (uint16_t)(1 + i));
  g_fpop = mkwin(WS_POPUP | WS_VISIBLE, 20, 20, 100, 100, g_fwin, 0);
  g_ficon = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MINIMIZE, 70, 80, 150, 90, 0, 0);
}
/* The research field of a player (low nibble of the row byte +0x39): 0 to 5. The research dialog (27:0x0000) loads the
 * string 0x54 + field into a 0x2e byte stack buffer; 14 is the 100 character string 0x62 and smashes the frame. */
static void clamp_research(void) {
  for (unsigned k = 0; k < 16; k++) {
    uint8_t *b = &DS_B((uint16_t)(0x5a9d + 0xc0 * k));
    *b = (uint8_t)((*b & 0xf0) | ((*b & 0xf) % 6));
  }
}
/* The parts of 4:0x506c's input that must be sane for it to run at all (the tables of letters, the format
 * pointer, the mode); uses its own generator so the stream of the calling scenario is not disturbed. */
static void ini_tables(void) {
  uint32_t ts = g_rng ^ 0x9e3779b9u;
#define TR() (ts = ts * 1664525u + 1013904223u, ts >> 8)
#define TRN(n) (TR() % (n))
  static const char *const fmts[] = {"%d", "%u", "%x", "%04d"};
  memset(&DS_B(0x5866), 0, 0x100);
  for (unsigned i = 0; i < 0x70; i++) DS_B((uint16_t)(0x7c2 + i)) = (uint8_t)TR();
  for (unsigned i = 0; i < 0x80; i++) DS_B((uint16_t)(0x8d6 + i)) = (uint8_t)TR();
  for (unsigned i = 0; i < 4; i++) {
    const uint16_t bx = (uint16_t)(0x545f + 0x18 * i);
    unsigned len = TRN(13);
    for (unsigned k = 0; k < 10; k++) DS_B((uint16_t)(bx - 0x17 + k)) = (uint8_t)TR();
    for (unsigned k = 0; k < len; k++) DS_B((uint16_t)(bx - 0xd + k)) = (uint8_t)('a' + TRN(26));
    DS_B((uint16_t)(bx - 0xd + len)) = 0;
    DS_B(bx) = (uint8_t)(TRN(4) == 0 ? 0 : TR());
  }
  for (unsigned i = 0; i < 5; i++) {
    const uint16_t bx = (uint16_t)(0x2585 + 0x28 * i);
    unsigned len = TRN(13);
    for (unsigned k = 0; k < len; k++) DS_B((uint16_t)(bx - 0xf + k)) = (uint8_t)('A' + TRN(26));
    DS_B((uint16_t)(bx - 0xf + len)) = 0;
    DS_B((uint16_t)(bx - 2)) = (uint8_t)(TRN(4) == 0 ? 0 : 1 + TRN(200));
    DS_B((uint16_t)(bx - 1)) = (uint8_t)TRN(10);
    {
      const unsigned few = TRN(5) == 0;
      DS_B(bx) = (uint8_t)TRN(few ? 1 : 7);
    }
    for (unsigned k = 0; k < 24; k++) DS_B((uint16_t)(bx + 1 + k)) = (uint8_t)TR();
  }
  static const uint16_t modes[] = {0, 1, 2, 4};
  DS_W(0x4b74) = modes[TRN(4)];
  strcpy((char *)&DS_B(SCR + 0x400), fmts[TRN(4)]);
  DS_W(0x016a) = SCR + 0x400;
#undef TR
#undef TRN
}
static void fw_state(void) {
  stubs_clear_all();
  input_reset();
  players(1 + rn(4));
  sane_names();
  DS_W(0x2806) = g_fwin;
  DS_W(0x0150) = g_fkid[0];
  DS_W(0x0154) = g_fkid[1];
  DS_W(0x0156) = g_fkid[2];
  DS_W(0x0158) = g_fkid[3];
  DS_W(0x015a) = g_fkid[4];
  DS_W(0x54f4) = MEM_SEL_DGROUP;
  DS_W(0x255e) = (uint16_t)rn(4);
  DS_W(0x264a) = (uint16_t)(300 + rn(500));
  DS_W(0x264c) = (uint16_t)(200 + rn(400));
  DS_W(0x267a) = (uint16_t)(8 + rn(20));
  DS_W(0x264e) = (uint16_t)(198 + rn(200));
  DS_W(0x2650) = (uint16_t)(rn(200));
  DS_W(0x2652) = (uint16_t)(DS_W(0x2650) + 8 + rn(100));
  DS_W(0x2654) = (uint16_t)rn(300);
  DS_W(0x2656) = (uint16_t)rn(100);
  DS_W(0x2658) = (uint16_t)rn(100);
  DS_W(0x265a) = (uint16_t)rn(300);
  DS_W(0x265c) = (uint16_t)rn(100);
  DS_W(0x265e) = (uint16_t)rn(100);
  DS_W(0x0010) = GetStockObject(0);
  DS_W(0x0014) = GetStockObject(4);
  DS_W(0x0016) = GetStockObject(1);
  DS_W(0x001e) = GetStockObject(2);
  DS_W(0x0316) = 0x0e01;
  DS_W(0x0318) = 0x0e02;
  DS_W(0x031a) = 0x0e03;
  DS_B(0x0793) = (uint8_t)rnd();
  DS_B(0x0790) = (uint8_t)rnd();
  DS_B(0x0791) = (uint8_t)rnd();
  DS_B(0x0792) = (uint8_t)rnd();
  DS_W(0x0366) = (uint16_t)rnd();
  DS_W(0x076e) = (uint16_t)rn(3);
  DS_W(0x014c) = rn(4) == 0 ? 0xffff : (uint16_t)rn(3);
  DS_W(0x0162) = (uint16_t)rn(2) * 7;
  DS_W(0x0164) = (uint16_t)(rn(3) + 0xc);
  ds_w32(0x0070, rn(2) ? 0 : 0x12345678);
  ds_w32(0x0872, 0);
  DS_W(0x0312) = 0;
  DS_W(0x0314) = 0;
  DS_W(0x4b74) = (uint16_t)rn(4);
  strcpy((char *)&DS_B(SCR + 0x200), "G%d");
  DS_W(0x0190) = SCR + 0x200;
  memset(&DS_B(0x5764), 0, 0x40);
  strcpy((char *)&DS_B(0x5764), "C:\\STARS\\GAME.HST");
  ini_tables(); /* 4:0x506c runs for real from WM_DESTROY and the commands */
  clamp_research();
}
#define FW(hw, m, wp, lphi, lplo) DIFF(4, 0x0528, FAR_PASCAL, (hw), (m), (wp), (lphi), (lplo))
/* ---- 4:0x143a: the divider drag bars ---- */
/* The DC is the main window's; PatBlt runs for real. The previous delta (prev) is a pair of words in DGROUP. */
static void t_143a(void) {
  uint16_t win = menu_window();
  uint16_t dc = GetDC(win);
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x143a0001u + 0x9e3779b9u * (it + 1);
    DS_W(0x255e) = (uint16_t)rn(4);
    DS_W(0x264a) = (uint16_t)(rn(8) == 0 ? rnd() : 200 + rn(700));
    DS_W(0x264c) = (uint16_t)(rn(8) == 0 ? rnd() : 100 + rn(500));
    DS_W(0x264e) = (uint16_t)(rn(8) == 0 ? rnd() : 150 + rn(300));
    DS_W(0x2650) = (uint16_t)(rn(8) == 0 ? rnd() : rn(300));
    DS_W(0x2652) = (uint16_t)(rn(8) == 0 ? rnd() : rn(400));
    DS_W(0x267a) = (uint16_t)(rn(8) == 0 ? rnd() : 4 + rn(30));
    static const uint16_t bitsv[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 0xffff, 7, 5, 3, 2, 4};
    uint16_t bits = bitsv[rn(15)];
    int16_t dx = (int16_t)(rn(6) == 0 ? (int)rnd() : (int)rn(500) - 250);
    int16_t dy = (int16_t)(rn(6) == 0 ? (int)rnd() : (int)rn(500) - 250);
    uint16_t prev = 0;
    if (rn(3) != 0) {
      prev = SCR + 0x300;
      DS_W(prev) = (uint16_t)(rn(5) == 0 ? rnd() : (int)dx + (int)rn(40) - 20);
      DS_W(prev + 2) = (uint16_t)(rn(5) == 0 ? rnd() : (int)dy + (int)rn(40) - 20);
    }
    DIFF(4, 0x143a, FAR_CDECL, dc, bits, (uint16_t)dx, (uint16_t)dy, prev);
  }
}

/* ---- 4:0x379a: the menu state ---- */
/* The menu is a real runtime menu (menu_window: six pop-ups); DeleteMenu / InsertMenu / EnableMenuItem / CheckMenuItem
 * run for real in both runs; the file name table DS:0x5f6 is a block of nine strings, 0x100 bytes apart. */
static void t_379a(void) {
  fw_windows();
  uint32_t strs = gblock(0x900);
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x379a0001u + 0x9e3779b9u * (it + 1);
    fw_state();
    for (unsigned k = 0; k < 9; k++) {
      char *p = (char *)LIN(strs) + 0x100 * k;
      memset(p, 0, 0x20);
      if (rn(5) != 0 || k == 0) snprintf(p, 0x20, "GAME%u.HST", rn(100));
      if (rn(5) == 0) p[0] = 0;
    }
    ds_w32(0x05f6, rn(6) ? strs : 0);
    if (DS_W(0x05f6) == 0 && DS_W(0x05f8) == 0) ds_w32(0x05f6, strs + 0x00); /* the table pointer must be valid */
    memset(&DS_B(0x5764), 0, 4);
    if (rn(3)) DS_B(0x5764) = (uint8_t)(rn(3) == 0 ? 1 : 'C');
    DS_B(0x0080) = (uint8_t)rnd();
    DS_B(0x0793) = (uint8_t)rnd();
    DS_B(0x055f) = (uint8_t)rnd();
    ds_w32(0x031c, (uint32_t)((int32_t)rn(5) - 2));
    DS_W(0x0150) = rn(2) ? g_fkid[0] : 0;
    DS_W(0x0576) = (uint16_t)rn(6);
    DS_W(0x255e) = (uint16_t)rn(5);
    uint16_t bar = GetMenu(g_fwin);
    const uint16_t menus[] = {0, bar, bar, 0x7777};
    DIFF(4, 0x379a, FAR_CDECL, menus[rn(4)]);
  }
}

/* ---- 4:0x4cec HOSTOPTIONSDIALOG ---- */
static void t_4cec(void) {
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  uint16_t dc = GetDC(win);
  DS_W(0x0010) = GetStockObject(0);
  ds_w32(0x253a, 0x00c0c0c0);
  DS_W(0x010a) = SCR + 0x240;
  strcpy((char *)&DS_B(SCR + 0x240), "STARS.HLP");
  static const uint16_t msgs[] = {0x000f, 0x0014, 0x0019, 0x0110, 0x0111, 0x0000, 0x0001, 0x0013, 0x0015, 0x0018, 0x001a, 0x010f, 0x0112, 0x7fff};
  const uint16_t wps[] = {0, 1, 2, 3, 0x76, 0x75, 0x77, dc, 0xffff};
  for (unsigned m = 0; m < sizeof msgs / 2; m++) {
    for (unsigned w = 0; w < sizeof wps / 2; w++) {
      DIFF(4, 0x4cec, FAR_PASCAL, win, msgs[m], wps[w], 0x1234, 0x5678);
      DIFF(4, 0x4cec, FAR_PASCAL, 0, msgs[m], wps[w], 0, 0);
    }
  }
}

/* ---- 4:0x41f8: the player list of the host-mode dialog ---- */
/* No doubles: 5:0x34fc (decompiled), 8:0x9398, 8:0x8dc0 / 0x8df4 and 7:0x0b84 run for real in both runs. The window is an
 * ordinary window with the two child windows 0x7e0 and 0x7e1 (the elapsed time and the turn). */
static void t_41f8(void) {
  uint16_t win = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0);
  (void)mkwin(WS_CHILD | WS_VISIBLE, 0, 0, 50, 20, win, 0x7e0);
  (void)mkwin(WS_CHILD | WS_VISIBLE, 0, 30, 50, 20, win, 0x7e1);
  uint16_t dc = GetDC(win);
  for (unsigned it = 0; it < 250; it++) {
    g_rng = 0x41f80001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    players(1 + rn(6));
    sane_names();
    DS_W(0x267a) = (uint16_t)(8 + rn(20));
    DS_W(0x2930) = GetStockObject(16);
    ds_w32(0x253a, 0x00c0c0c0);
    ds_w32(0x4ac0, 0x00123456);
    DS_W(0x5a5c) = (uint16_t)rnd();
    DS_B(0x0796) = (uint8_t)rnd();
    DS_W(0x0082) = (uint16_t)rn(40);
    for (unsigned k = 0; k < 16; k++) DS_W(0x6666 + 2 * k) = (uint16_t)((int)rn(7) - 1); /* -1 (a computer player), 0 to 5: the states 4:0x4580 writes */
    /* the stamp: 0 to 3 days before the clock */
    uint32_t now = GetTickCount();
    static const uint32_t spans[] = {0, 1000, 59000, 60000, 61000, 3599000, 3600000, 3601000, 86399000u, 86400000u, 90061000u, 300000000u};
    ds_w32(0x0192, now - spans[rn(12)] - (rn(2) ? rn(1000) : 0));
    uint16_t hd = rn(3) == 0 ? 0 : dc;
    DIFF(4, 0x41f8, FAR_CDECL, rn(10) ? win : 0, hd);
  }
}

/* ---- 4:0x467c HOSTMODEDIALOG ---- */
/* Doubles: 14:0x54ea, 8:0xb1ec, 8:0x8d94, 9:0x7c26 (4:0x4580's file check), 8:0xb90e... as in t_4090. The pop-up menu of a
 * click is answered by queued keys (Down, Return picks the first item; Escape none); 8:0x97a2 (window position),
 * 8:0x8dc0 / 0x8df4, 4:0x4580, 4:0x41f8 and 4:0x39f8 run for real. */
static uint16_t g_hm_dlg;
static void hm_state(void) {
  stubs_clear_all();
  input_reset();
  state_world(1 + rn(5));
  sane_names();
  DS_W(0x267a) = (uint16_t)(8 + rn(12));
  DS_W(0x2930) = GetStockObject(16);
  DS_W(0x0010) = GetStockObject(0);
  ds_w32(0x253a, 0x00c0c0c0);
  ds_w32(0x4ac0, 0x00123456);
  DS_W(0x5442) = 0x0e11;
  DS_W(0x54f4) = rn(3) ? 0 : MEM_SEL_DGROUP;
  DS_W(0x03c0) = (uint16_t)(rn(8) == 0);
  DS_W(0x0162) = (uint16_t)rn(2);
  DS_W(0x416a) = (uint16_t)rn(2);
  DS_W(0x416c) = (uint16_t)rn(2);
  DS_B(0x0794) = (uint8_t)rnd();
  DS_W(0x0366) = (uint16_t)rnd();
  DS_W(0x0082) = (uint16_t)rn(40);
  DS_W(0x010a) = SCR + 0x240;
  strcpy((char *)&DS_B(SCR + 0x240), "STARS.HLP");
  DS_B(0x796) = (uint8_t)rnd();
  ds_w32(0x0192, rn(3) ? 0 : GetTickCount() - 1000 * rn(100));
  DS_W(0x14c) = (uint16_t)rn(3);
  memset(&DS_B(0x5764), 0, 0x20);
  strcpy((char *)&DS_B(0x5764), "C:\\STARS\\GAME.HST");
  stub_def(S_9_7C26, rn(4) == 0, 0);
  stub_out(S_9_7C26, 0);
  stub_def(S_14_54EA, rn(2), 0);
  stub_def(S_8_B1EC, rn(2), 0);
  for (unsigned k = 0; k < 3; k++) stub_push(S_8_8D94, rn(2) ? 6 : (uint16_t)rn(8), 0);
  {
    static const uint16_t quit[] = {8, 0x0108, 0x18};
    uint16_t qv = quit[rn(3)];
    stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
  }
  stub_def(S_14_2CAA, 1, 0);
}
static void t_467c(void) {
  g_hm_dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0);
  static const uint16_t kids[] = {0x407, 0x408, 0x409, 0x40a, 0x7df, 0x7e0, 0x7e1};
  for (unsigned i = 0; i < 7; i++) (void)mkwin(WS_CHILD | WS_VISIBLE, 0, 20 * (int)i, 50, 18, g_hm_dlg, kids[i]);
  uint16_t dc = GetDC(g_hm_dlg);
  /* ---- the plain messages ---- */
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x467c0001u + 0x9e3779b9u * (it + 1);
    hm_state();
    static const uint16_t msgs[] = {0x0002, 0x000f, 0x0014, 0x0019, 0x0110, 0x0113, 0x0000, 0x0001, 0x0013, 0x0015, 0x001a, 0x0112, 0x7fff};
    DIFF(4, 0x467c, FAR_PASCAL, g_hm_dlg, msgs[rn(13)], rn(2) ? dc : (uint16_t)rnd(), 0, 0);
  }
  /* ---- the mouse: the cursor over the list, then a pop-up answered by keys ---- */
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x467c0101u + 0x9e3779b9u * (it + 1);
    hm_state();
    int h = (int)DS_W(0x267a), row = (int)rn(DS_W(0x78) + 1), px = 6 + (int)rn(h + 3) - (rn(5) == 0 ? 8 : 0);
    int py = 0x30 + row * (h + 4) + (int)rn(h + 3) - (rn(6) == 0 ? 6 : 0);
    uint32_t pt = gblock(8);
    ((POINT16 *)LIN(pt))->x = (int16_t)px;
    ((POINT16 *)LIN(pt))->y = (int16_t)py;
    ClientToScreen(g_hm_dlg, pt);
    rt_input_mouse(((POINT16 *)LIN(pt))->x, ((POINT16 *)LIN(pt))->y, 0, 0);
    if (rn(2)) {
      key_tap(VK_DOWN, 0x50);
      if (rn(2)) key_tap(VK_DOWN, 0x50);
      if (rn(2)) key_tap(VK_DOWN, 0x50);
      key_tap(VK_RETURN, 0x1c);
    }
    key_tap(VK_ESCAPE, 0x01);
    static const uint16_t msgs[] = {0x0020, 0x0201, 0x0204};
    DS_W(0x14c) = (uint16_t)rn(3);
    DIFF(4, 0x467c, FAR_PASCAL, g_hm_dlg, msgs[rn(3)], 0, 0, 0);
  }
  /* ---- WM_COMMAND ---- */
  for (unsigned it = 0; it < 200; it++) {
    g_rng = 0x467c0201u + 0x9e3779b9u * (it + 1);
    hm_state();
    static const uint16_t ids[] = {0x407, 0x407, 0x407, 2, 0x408, 0x7df, 0x405, 0x76, 1, 0x409, 0};
    uint16_t id = ids[rn(11)];
    unsigned held = rn(5);
    if (held == 1) rt_input_key(VK_SHIFT, 0x2a, 1, 0);
    if (held == 2) rt_input_key(VK_CONTROL, 0x1d, 1, 0);
    if (held == 3) {
      rt_input_key(VK_SHIFT, 0x2a, 1, 0);
      rt_input_key(VK_CONTROL, 0x1d, 1, 0);
    }
    key_tap(rn(2) ? VK_RETURN : 'N', 0x31);
    for (unsigned k = 0; k < 3; k++) key_tap(VK_ESCAPE, 0x01);
    DIFF(4, 0x467c, FAR_PASCAL, g_hm_dlg, 0x0111, id, 0, 0);
  }
  input_reset();
}

/* A screen point from a point of the client area of the main window. */
static void fw_screen_point(int cx, int cy, int *sx, int *sy) {
  uint32_t pt = gblock(8);
  POINT16 *p = LIN(pt);
  p->x = (int16_t)cx;
  p->y = (int16_t)cy;
  ClientToScreen(g_fwin, pt);
  *sx = p->x;
  *sy = p->y;
}
/* The text block 4:0x0528 copies lines from (DS:0x15e is the read pointer, DS:0x5394 the end). */
__attribute__((unused)) static void fw_text(void) {
  static const char txt[] = "C:\\STARS\\A.HST\r\nC:\\STARS\\B.HST\r\n\nX\n";
  uint32_t t = gblock(sizeof txt + 4);
  memcpy(LIN(t), txt, sizeof txt);
  uint32_t at = t + (uint32_t)rn(3) * 14;
  ds_w32(0x015e, at);
  ds_w32(0x5394, rn(4) ? t + (uint32_t)(sizeof txt - 1) : at + (uint32_t)rn(5));
}
static bool fwp(const char *part) { return !g_only || strstr(g_only, "0528") || strstr(g_only, part); }
static void t_0528(void) {
  fw_windows();
  /* ---- the plain messages ---- */
  for (unsigned it = 0; it < (fwp("fwa") ? 8u : 0u); it++) {
    g_rng = 0x05280001u + 0x9e3779b9u * (it + 1);
    fw_state();
    FW(g_fwin, 0x0001, 0, 0, 0);
    FW(g_fwin, 0x0002, 0, 0, 0);
    FW(g_fwin, 0x0005, (uint16_t)rn(4), (uint16_t)(100 + rn(500)), (uint16_t)(100 + rn(400)));
    FW(g_fwin, 0x0005, 2, 0x7fff, 0x8000);
    FW(g_fwin, 0x000f, 0, 0, 0);
    FW(g_ficon, 0x000f, 0, 0, 0);
    {
      uint16_t dc = GetDC(g_fwin);
      FW(g_fwin, 0x0014, dc, 0, 0);
      FW(g_ficon, 0x0014, dc, 0, 0);
      DS_W(0x0150) = 0;
      FW(g_fwin, 0x0014, dc, 0, 0);
      DS_W(0x0150) = g_fkid[0];
      ReleaseDC(g_fwin, dc);
    }
    FW(mkwin(WS_OVERLAPPED | WS_VISIBLE, 5, 5, 30, 30, 0, 0), 0x0010, 0, 0, 0);
    FW(g_fwin, 0x0015, 0, 0, 0);
    FW(g_fwin, 0x001a, 0, 0, 0);
    {
      uint32_t mm = gblock(0x20);
      FW(g_fwin, 0x0024, 0, HI(mm), LO(mm));
    }
    FW(g_fwin, 0x0037, 0, 0, 0);
    FW(g_fwin, 0x0003, 0, 0, 0);
    FW(g_fwin, 0x0016, 0, 0, 0);
    FW(g_fwin, 0x0000, 0, 0, 0);
    FW(g_fwin, 0x0400, 1, 2, 3);
    FW(g_fwin, 0x0006, 1, 2, 3);
    FW(g_fwin, 0x0116 + 1, 0, 0, 0);
  }
  /* ---- WM_SETCURSOR: the point under the cursor and the marker squares of 4:0x18b0 ---- */
  for (unsigned it = 0; it < (fwp("fwb") ? 60u : 0u); it++) {
    g_rng = 0x05280101u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x2670) = 0x1111;
    DS_W(0x53b2) = 0x2222;
    DS_W(0x2940) = 0x3333;
    static const int16_t d[] = {-9, -8, -1, 0, 1, 7, 8, 9};
    int cxp = (int)(int16_t)DS_W(0x264e) + d[rn(8)] + (int)rn(2) * 40, cyp = (int)(int16_t)DS_W(rn(2) ? 0x2650 : 0x2652) + d[rn(8)];
    if (rn(5) == 0) cxp = (int)rn(400) - 20;
    int sx, sy;
    fw_screen_point(cxp, cyp, &sx, &sy);
    rt_input_mouse((int16_t)sx, (int16_t)sy, 0, 0);
    FW(rn(4) ? g_fwin : g_ficon, 0x0020, 0, 0, 0);
  }
  /* ---- WM_CHAR ---- */
  for (unsigned it = 0; it < (fwp("fwc") ? 160u : 0u); it++) {
    g_rng = 0x05280201u + 0x9e3779b9u * (it + 1);
    fw_state();
    static const uint16_t keys[] = {0x2d, 0x2b, 0x0d, 0x66, 0x46, 0x71, 0x51, 0x6e, 0x4e, 0x50, 0x70, 0x72, 0x52, 0x41, 0, 0x7fff};
    if (rn(4) == 0) DS_W(0x0150) = 0;
    if (rn(4) == 0) DS_W(0x0158) = 0;
    if (rn(4) == 0) DS_W(0x0156) = 0;
    DS_W(0x4b74) = (uint16_t)rn(4);
    DS_W(0x4c08) = (uint16_t)rnd();
    DS_W(0x291e) = (uint16_t)rnd();
    stub_def(S_9_3082, rn(2) ? 0 : (uint16_t)rnd(), 0);
    FW(g_fwin, 0x0102, keys[rn(16)], (uint16_t)rnd(), (uint16_t)rnd());
  }
  /* ---- WM_COMMAND, 0x116, 0x121 ---- */
  for (unsigned it = 0; it < (fwp("fwd") ? 30u : 0u); it++) {
    g_rng = 0x05280301u + 0x9e3779b9u * (it + 1);
    fw_state();
    FW(g_fwin, 0x0111, (uint16_t)rnd(), (uint16_t)rnd(), (uint16_t)rnd());
    FW(g_fwin, 0x0116, (uint16_t)rnd(), (uint16_t)rnd(), (uint16_t)rnd());
    FW(g_fwin, 0x0121, (uint16_t)rnd(), (uint16_t)rnd(), (uint16_t)rnd());
  }
  /* ---- WM_SYSCOMMAND ---- */
  for (unsigned it = 0; it < (fwp("fwe") ? 250u : 0u); it++) {
    g_rng = 0x05280401u + 0x9e3779b9u * (it + 1);
    fw_state();
    static const uint16_t cmds[] = {0xf030, 0xf03f, 0xf120, 0xf12f, 0xf020, 0xf040, 0xf130, 0xf02f, 0, 0xffff};
    DS_W(0x0164) = (uint16_t)(rn(5) + 0xb);
    DS_W(0x0162) = rn(3) ? (uint16_t)(1 + rn(5)) : 0;
    ds_w32(0x4b70, rn(3) ? 0x03e903e9u : rnd());
    stub_push(S_14_2F40, rn(3) != 0, 0);
    for (unsigned k = 0; k < 3; k++) stub_push(S_8_8D94, rn(3) == 0 ? 6 : rn(2) ? 2 : (uint16_t)rn(8), 0);
    stub_push(S_14_0508, rn(3) != 0, 0);
    stub_push(S_14_54EA, rn(2), 0);
    FW(g_fwin, 0x0112, cmds[rn(10)], (uint16_t)rnd(), (uint16_t)rnd());
  }
  /* the state 0xe of the timer with "the next player's file" refused to load (4:0x0cfe and following) */
  for (unsigned it = 0; it < (fwp("fwe") ? 12u : 0u); it++) {
    g_rng = 0x05280451u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x0164) = 0x0e;
    DS_W(0x0162) = 5;
    DS_W(0x014c) = rn(2) ? 0xffff : (uint16_t)rn(3);
    stub_push(S_14_2F40, 0, 0);
    stub_push(S_8_8D94, 6, 0);
    stub_push(S_8_8D94, 0x10, 0);
    stub_push(S_8_8D94, 0x10, 0);
    stub_push(S_14_54EA, it & 1, 0);
    stub_push(S_14_0508, (it >> 1) & 1, 0);
    FW(g_fwin, 0x0112, 0xf030, 0, 0);
  }
  /* ---- WM_LBUTTONDOWN: dragging a divider (4:0x18b0 decides which, 4:0x143a draws, 8:0xa2c6 reads the mouse) ---- */
  for (unsigned it = 0; it < (fwp("fwf") ? 50u : 0u); it++) {
    g_rng = 0x05280501u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x2670) = 0x1111;
    DS_W(0x53b2) = 0x2222;
    DS_W(0x2940) = 0x3333;
    DS_W(0x4d92) = GetStockObject(1);
    int c0 = (int)(int16_t)DS_W(0x264e), c1 = (int)(int16_t)DS_W(0x2650), c2 = (int)(int16_t)DS_W(0x2652);
    int x = c0 + (int)rn(8) - (rn(4) == 0 ? 8 : 0), y = (rn(2) ? c1 : c2) + (int)rn(8) - (rn(4) == 0 ? 6 : 0);
    int sx, sy;
    fw_screen_point(x, y, &sx, &sy);
    rt_input_mouse((int16_t)sx, (int16_t)sy, 1, 0);
    unsigned nm = rn(5);
    for (unsigned k = 0; k < nm; k++) {
      key_tap('A', 0x1e);
      int mx, my;
      fw_screen_point(x + (int)rn(120) - 60, y + (int)rn(120) - 60, &mx, &my);
      rt_input_mouse((int16_t)mx, (int16_t)my, 1, 0);
    }
    {
      int mx, my;
      fw_screen_point(x + (int)rn(120) - 60, y + (int)rn(120) - 60, &mx, &my);
      rt_input_mouse((int16_t)mx, (int16_t)my, 0, 0);
    }
    if (DIFF(4, 0x0528, FAR_PASCAL, g_fwin, 0x0201, 0, (uint16_t)y, (uint16_t)x) != DT_PASS)
      fprintf(stderr, "  0528 0x201 iteration %u: x=%d y=%d c0=%d c1=%d c2=%d mode=%u\n", it, x, y, c0, c1, c2, DS_W(0x255e));
  }
  /* ---- the palette messages ---- */
  for (unsigned it = 0; it < (fwp("fwg") ? 16u : 0u); it++) {
    g_rng = 0x05280601u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x0312) = rn(2) ? g_fkid[4] : 0;
    DS_W(0x54f8) = rn(3) ? g_fpal : 0;
    uint16_t wp = rn(3) ? g_fkid[1] : g_fwin;
    FW(g_fwin, 0x030f, wp, 0, 0);
    FW(g_fwin, 0x0311, wp, 0, 0);
  }
  /* ---- 0x465 (the host-mode dialog loop) and 0x466 (load a game file) ---- */
  for (unsigned it = 0; it < (fwp("fwh") ? 60u : 0u); it++) {
    g_rng = 0x05280701u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_B(0x251a) = (uint8_t)(rnd() | 4);
    DS_W(0x14c) = (uint16_t)rn(3);
    ds_w32(0x53e2, rnd());
    stub_def(S_9_7C26, rn(5) == 0, 0);
    {
      static const uint16_t quit[] = {8, 0x0108, 0x18}; /* 4:0x4dba counts down until bit 3 of DS:0x251a says "quit" */
      uint16_t qv = quit[rn(3)];
      stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
    }
    key_tap(VK_ESCAPE, 0x01);
    FW(g_fwin, 0x0465, 0, 0, 0);
  }
  for (unsigned it = 0; it < (fwp("fwh") ? 80u : 0u); it++) {
    g_rng = 0x05280801u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x158) = rn(3) ? g_fkid[3] : 0;
    stub_push(S_14_0508, rn(3) != 0, 0);
    stub_def(S_9_78A0, rn(2), 0);
    FW(g_fwin, 0x0466, rn(2) ? 0x09ca : (uint16_t)rn(4), 0, 0);
  }
  /* ---- 0x464: start-up / continue after a turn ---- */
  for (unsigned it = 0; it < (fwp("fwi") ? 200u : 0u); it++) {
    g_rng = 0x05280901u + 0x9e3779b9u * (it + 1);
    fw_state();
    DS_W(0x251a) = (uint16_t)rnd();
    if (rn(3) == 0) DS_B(0x251a) |= 2;
    if (rn(4) == 0) DS_B(0x251a) &= (uint8_t)~8;
    DS_W(0x2526) = (uint16_t)rn(3);
    fw_text();
    if (rn(2)) {
      ds_w32(0x0872, rnd() | 1);
      for (unsigned k = 0; k < 11; k++) DS_B(0x5382 + k) = DS_B(0x564c + k) = (uint8_t)rnd();
      if (rn(2)) DS_B(0x5382 + rn(11)) ^= 1;
    }
    DS_W(0x0312) = rn(2) ? g_fkid[4] : 0;
    DS_W(0x0314) = (uint16_t)rn(2);
    ds_w32(0x0070, rn(2) ? 0 : 0x12345678);
    stub_def(S_9_7C26, rn(5) == 0, 0);
    if (rn(2)) stub_eff_ds(S_4_1F40, 0, 0x014c, &(uint16_t){(uint16_t)rn(3)}, 2); /* the command sets a current player */
    stub_eff_arg(S_7_3EF4, -1, 1, 0, &(uint32_t){rnd() | 1}, 4);
    stub_def(S_7_3EF4, rn(2), 0);
    DS_W(0x5394 + 2) = DS_W(0x5394 + 2);
    key_tap(rn(2) ? VK_RETURN : VK_ESCAPE, 0x1c);
    key_tap(VK_ESCAPE, 0x01);
    key_tap(VK_ESCAPE, 0x01);
    key_tap(VK_ESCAPE, 0x01);
    FW(g_fwin, 0x0464, 0, 0, 0);
  }
}

/* ---- 4:0x1f40: the WM_COMMAND handler ---- */
/* Doubles (logged thunks): 32:0x67a8 / 0x5ada / 0x5954, 10:0x606a, 8:0x8d94, 9:0x80da / 0x7eea / 0x641c / 0x7d1e /
 * 0x431c / 0x07e0 / 0x3aa8, 14:0x2f88 / 0x30d8 / 0x35b0 / 0x2f40 / 0x2992 / 0x0508, 31:0x64f8 / 0x5f14 / 0x5cc2 /
 * 0x61b0, 15:0x6ab2, 25:0x0000, 28:0x0000, 11:0x0ab0 / 0x4c62 / 0x5142, 29:0x01c2, 8:0xb8cc / 0xb8f6 / 0xb90e, 22:0x0000,
 * 4:0x506c, 4:0x379a. Run for real: 4:0x3b66 / 0x3ecc (the file dialog is the runtime's, answered with Escape), 4:0x4090
 * and the host-mode dialog, 4:0x4dba, 4:0x39f8, 4:0x197a, 4:0x02ca, 4:0x5d98, 4:0x3b30, the About dialog 3:0x0ca0,
 * 7:0x0b84, 34:0x0922 / 0x0578 / 0x0352, 8:0x8df4 / 0x8672. The dialogs behind MakeProcInstance / DialogBox are the
 * original ones when DS:0x54f4 is the instance; mostly it is 0, which makes DialogBox fail (-1) at once. Blind spots:
 * the printing body behind PrintDlg (the runtime answers FALSE), what the doubles would do, the dialog procedures. */
static void fc_state(void) {
  fw_state();
  ds_w32(0x0070, rn(5) ? 0x12345678u : 0);
  const uint16_t want14c = rn(4) == 0 ? 0xffff : (uint16_t)rn(3);
  DS_W(0x014c) = want14c;
  DS_B(0x0080) = (uint8_t)rnd();
  DS_B(0x0791) = (uint8_t)rnd();
  DS_B(0x0792) = (uint8_t)rnd();
  DS_B(0x0793) = (uint8_t)rnd();
  DS_B(0x0794) = (uint8_t)rnd();
  DS_B(0x251a) = (uint8_t)(rnd() & 0xf3);
  DS_W(0x54f4) = rn(4) == 0 ? MEM_SEL_DGROUP : 0;
  DS_W(0x083a) = rn(3) == 0 ? 0x7777 : rn(2) ? 0 : g_fwin;
  DS_W(0x0378) = (uint16_t)rn(3);
  DS_W(0x007e) = (uint16_t)rn(2);
  DS_W(0x0082) = (uint16_t)rn(40);
  DS_W(0x0770) = (uint16_t)rnd();
  DS_W(0x014e) = rn(3) ? 0 : g_fkid[2];
  DS_W(0x0312) = rn(2) ? g_fkid[4] : 0;
  if (rn(5) == 0) DS_W(0x0150) = 0;
  DS_B(0x055f) = (uint8_t)rnd();
  DS_W(0x4b80) = (uint16_t)rn(2);
  ds_w32(0x4b7c, rnd());
  DS_W(0x27ea) = (uint16_t)rn(300);
  DS_W(0x27ec) = (uint16_t)rn(300);
  stub_def(S_8_B1EC, rn(2), 0);
  DS_W(0x177e) = 0;
  DS_W(0x1766) = (uint16_t)(rn(2) ? 0x164e : 0x1684);
  DS_W(0x0576) = (uint16_t)(rn(8));
  DS_W(0x007a) = (uint16_t)rn(4);
  DS_W(0x0bbe) = rn(2) ? g_fkid[3] : 0;
  ds_w32(0x4c3c, rn(2) ? 0 : rnd());
  ds_w32(0x031c, (uint32_t)((int32_t)rn(5) - 2));
  ds_w32(0x53b4, MK_FP(g_test_proc_sel, 0));
  ds_w32(0x53e2, rnd());
  {
    const uint16_t cur = DS_W(0x014c) == 0xffff ? 0 : DS_W(0x014c);
    (void)cur;
    pf_world(rn(MAXP + 1), rn(MAXP + 1), want14c == 0xffff ? 0 : want14c, rn(2));
    DS_W(0x014c) = want14c;
    if (rn(6) == 0) {
      ds_w32(0x00b6, 0);
      DS_W(0x4d96) = 0;
    }
  }
  DS_W(0x010a) = SCR + 0x240;
  strcpy((char *)&DS_B(SCR + 0x240), "STARS.HLP");
  stub_def(S_9_7EEA, rn(4) != 0, 0);
  stub_def(S_14_2F88, rn(3) == 0, 0);
  stub_def(S_14_30D8, rn(2), 0);
  stub_def(S_31_64F8, rn(4) != 0, 0);
  stub_def(S_11_0AB0, (uint16_t)rn(500), 0);
  stub_def(S_29_01C2, (uint16_t)rn(3), 0);
  stub_push(S_14_2F40, rn(3) != 0, 0);
  stub_push(S_14_2F40, rn(3) != 0, 0);
  for (unsigned k = 0; k < 4; k++) stub_push(S_8_8D94, rn(3) == 0 ? 6 : rn(2) ? 2 : (uint16_t)rn(8), 0);
  for (unsigned k = 0; k < 3; k++) stub_push(S_14_0508, rn(3) != 0, 0);
  stub_def(S_14_54EA, rn(2), 0);
  stub_def(S_9_7C26, rn(5) == 0, 0);
  stub_def(S_14_2CAA, 1, 0); /* 4:0x39f8 never reaches 17:0x0000 (which needs a current player) */
  {
    static const uint16_t quit[] = {8, 0x0108, 0x18};
    uint16_t qv = quit[rn(3)];
    stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
  }
  /* the message box of the countdown question (Return = Yes, N = No), then Escape for the dialogs */
  key_tap(rn(2) ? VK_RETURN : 'N', 0x31);
  for (unsigned i = 0; i < 4; i++) key_tap(VK_ESCAPE, 0x01);
  clamp_research();
}
static const uint16_t fc_ids[] = {
    0x53,  0x54,  0x55,  0x5f,  0x60,  0x63,  0x67,  0x68,  0x69,  0x6a,  0x6c,  0x6d,  0x6e,  0x6f,  0x71,  0x7d,
    0x7e,  0x81,  0x82,  0x83,  0x84,  0x87,  0x88,  0x89,  0x8a,  0x9c,  0x9d,  0x9e,  0x9f,  0xb3,  0xfa,  0xfb,
    0xfc,  0xfd,  0x100, 0x101, 0x10e, 0x428, 0x7d9, 0x7da, 0x7db, 0x7dc, 0x7dd, 0x7de, 0x8fd, 0x8fe, 0x8ff, 0x900,
    0x901, 0x98d, 0x9c1, 0x9c2, 0x9c4, 0x9c5, 0xed8, 0xed9, 0xeda, 0xedb, 0xee2, 0xf3d, 0xf41, 0xf45, 0xfa1, 0x1068,
    0x1069, 0x10cc, 0x10ce, 0x10d4, 0x5208, 0x5209, 0x520a, 0x3a98, 0x3afb, 0x3afc, 0x3a97, 0x0000, 0x0001, 0x0056,
    0x00b4, 0xffff, 0x0102, 0x0fa2, 0x1067, 0x106a, 0x10cb, 0x10d5, 0x5207, 0x520b, 0x07df, 0x0902, 0x098c, 0x09c0,
    0x09c3, 0x09c6, 0x0ee1, 0x0f3c, 0x0f46, 0x00d4, 0x00d5, 0x00d6};
/* PrintDlg for the scenarios: fills the PRINTDLG16 (hDevMode +6, hDevNames +8, hDC +0xa) with a screen DC and answers TRUE.
 * The runtime answers FALSE (printing is a separate task), which would leave the printing body of 4:0x1f40 unreachable. */
static uint16_t test_printdlg(uint32_t pd) {
  uint8_t *q = LIN(pd);
  uint16_t dc = gdi_create_screen_dc();
  uint16_t dm = GlobalAlloc(0x42, 16), dn = GlobalAlloc(0x42, 16);
  memcpy(q + 6, &dm, 2);
  memcpy(q + 8, &dn, 2);
  memcpy(q + 0xa, &dc, 2);
  return 1;
}
static void t_1f40_hdl(HCall *c) { c->ret = test_printdlg((uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16)); }
static void t_1f40(void) {
  harness_register("COMMDLG", 20, t_1f40_hdl);
  byname_printdlg_hook = test_printdlg;
  fw_windows();
  CHECK_EQ(vfs_put("C:\\STARS\\GAME.HST", "h", 1), 0);
  uint32_t strs = gblock(0x900);
  for (unsigned k = 0; k < 9; k++) {
    char *p = (char *)LIN(strs) + 0x100 * k;
    static const char *names[] = {"GAME.HST", "x.y", "", "NODOT", "G.H.I", "A.B", "q.r", "T.U", "ZZZ.YY"};
    strcpy(p, names[k]);
  }
  for (unsigned k = 0; k < sizeof fc_ids / sizeof fc_ids[0]; k++) {
    const uint16_t id = fc_ids[k];
    /* the commands with long branch chains get more states */
    const bool many = id == 0x69 || id == 0x6c || id == 0x6d || id == 0x6e || id == 0x6a || id == 0x7da || id == 0x6f ||
                      id == 0xed9 || id == 0xed8 || id == 0xeda || id == 0xedb || (id >= 0x5208 && id <= 0x520a) ||
                      (id >= 0x8fd && id <= 0x901) || id == 0x10e || id == 0xd5 || id == 0x7e || id == 0x87 || id == 0x67 || id == 0x68 || (id >= 0x10cc && id <= 0x10d4) || id == 0xf3d;
    const unsigned nr = !g_only || strstr(g_only, "1f40") || strstr(g_only, "fcr") ? (many ? 40u : 3u) : 0u;
    for (unsigned round = 0; round < nr; round++) {
      g_rng = 0x1f400001u + 0x9e3779b9u * (round * 1000 + k + 1);
      fc_state();
      ds_w32(0x05f6, rn(5) ? strs : 0);
      if (id == 0xd5) {
        /* the map printing: pages DS:0x188a x DS:0x188c, the map scale DS:0xb0 / 0xb2 and the planet coordinates */
        DS_W(0x188a) = (uint16_t)(1 + rn(2));
        DS_W(0x188c) = (uint16_t)(1 + rn(2));
        if (rn(10) == 0) {
          DS_W(0x188a) = (uint16_t)(45 + rn(10)); /* more pixels than 0x7d00: the product is clamped */
          DS_W(0x188c) = 1;
        } else if (rn(10) == 0) {
          DS_W(0x188a) = 1;
          DS_W(0x188c) = (uint16_t)(60 + rn(10));
        }
        for (unsigned pl = 0; pl < 4; pl++) {
          static uint32_t designs[4];
          if (!designs[pl]) designs[pl] = gblock(0x8d * 16 + 16);
          for (unsigned q = 0; q < 16; q++) {
            const uint16_t kind = rn(2) ? 0x20 : 0x21;
            memcpy((uint8_t *)LIN(designs[pl]) + 0x8d * q, &kind, 2);
          }
          ds_w32((uint16_t)(0x010c + 4 * pl), designs[pl]);
        }
        {
          uint8_t *plt = LIN(g_pl);
          for (unsigned q = 0; q < MAXP; q++) {
            plt[0x38 * q + 5] = (uint8_t)rnd();
            plt[0x38 * q + 0x2c] = (uint8_t)rnd();
          }
        }
        DS_W(0x00b0) = (uint16_t)(rn(5) == 0 ? 1 + rn(3000) : 1500 + rn(500));
        DS_W(0x00b2) = (uint16_t)(1000 + rn(3000));
        DS_W(0x007a) = (uint16_t)rn(5);
        for (unsigned q = 0; q < 64; q++) DS_W((uint16_t)(0x31ba + 2 * q)) = (uint16_t)(900 + rn(2400));
        DS_W(0x055e) = (uint16_t)(rn(3) == 0 ? 5 : rn(5));
        DS_B(0x055f) = (uint8_t)rnd();
        if (rn(6) == 0) DS_W(0x014c) = 0xffff;
        else if (DS_W(0x014c) == 0xffff) DS_W(0x014c) = 0;
      }
      if (many) {
        /* a clock that moves on: the 500 ms wait of the countdown (TimerCount) ends */
        stub_eff_clock(S_8_B90E, 0, 1);
        stub_push(S_9_7EEA, rn(5) != 0, 0);
        stub_push(S_9_7EEA, rn(5) != 0, 0);
        DS_B(0x53f0) = (uint8_t)(rn(2) ? DS_B(0x53f0) | 8 : DS_B(0x53f0) & ~8);
        DS_B(0x0791) = (uint8_t)(rn(2) ? DS_B(0x0791) | 8 : DS_B(0x0791) & ~8);
        DS_B(0x0080) = (uint8_t)(rn(2) ? DS_B(0x0080) | 4 : DS_B(0x0080) & ~4);
        if (rn(2)) DS_W(0x0082) = (uint16_t)(0x20 + rn(8));
        if (rn(3) == 0) {
          rt_input_key(rn(2) ? VK_SHIFT : VK_CONTROL, 0x2a, 1, 0); /* held when the countdown asks for them */
        }
      }
      if ((id == 0x6d || id == 0xed9) && rn(2)) {
        /* "open a game file" ends with a loaded game: the current name (bit 0 of DS:0x251a), a file the doubles of
         * 4:0x3ecc call "not a game file", and 14:0x0508 loading it */
        script_3ecc(2);
        DS_B(0x251a) |= 1;
        DS_B(0x251a) &= (uint8_t)~8;
        DS_W(0x14c) = (uint16_t)rn(3);
        DS_W(0x0162) = (uint16_t)rn(2);
        DS_B(0x0080) = (uint8_t)(rnd() & ~0u);
        stub_push(S_14_0508, 1, 0);
        stub_push(S_14_0508, 1, 0);
        strcpy((char *)&DS_B(0x5764), "C:\\STARS\\GAME.HST");
        key_tap(VK_ESCAPE, 0x01);
      }
      if (id >= 0x8fd && id <= 0x901 && rn(2)) {
        /* a report window of the class DS:0x1f6 exists: closing it clears DS:0x177e */
        uint16_t rep = CreateWindow(MK_FP(MEM_SEL_DGROUP, 0x01f6), 0, WS_OVERLAPPED, 0, 0, 50, 50, 0, 0, 0x1234, 0);
        DS_W(0x177e) = rep;
        DS_W(0x1766) = (uint16_t)(rn(4) == 0 ? 0x16f0 : rn(2) ? 0x164e : 0x1684);
      }
      clock_t t0 = clock();
      const uint16_t hwA = rn(10) ? g_fwin : 0;
      DtStatus st = DIFF(4, 0x1f40, FAR_CDECL, hwA, id);
      if (st != DT_PASS) fprintf(stderr, "1f40 round %u id %x: %.2fs status %d\n", round, id, (double)(clock() - t0) / CLOCKS_PER_SEC, st);
    }
  }
}

/* ---- 4:0x506c: save the settings into the INI file ---- */
/* WritePrivateProfileString runs for real on the VFS (which lives in the arena, so DIFF compares the INI text
 * of both runs); the strings come from 8:0x8dc0 / 8:0x8df4 / 34:0x37e (original code), 4:0x1af2 and 4:0x500c
 * are decompiled and run as C in the C version. The two tables with letters keep strings short and counts
 * small (the value buffer DS:0x5866 is 0x100 bytes). DS:0x4b74 stays in {0, 1, 2, 4}: for 3 and above 4 the
 * mode letter at BP-1 is an uninitialised stack byte. */
static uint16_t g_zoom;
static void ini_world(void) {
  if (g_zoom == 0) g_zoom = mkwin(WS_OVERLAPPED | WS_VISIBLE | 0x01000000u, 5, 6, 120, 90, 0, 0);
  ini_tables();
  ds_w32(0x0872, rnd());
  for (unsigned i = 0; i < 11; i++) DS_B((uint16_t)(0x5382 + i)) = (uint8_t)(rn(4) == 0 ? rnd() : rn(6));
  DS_W(0x54f6) = (uint16_t)(rn(4) == 0 ? rnd() : rn(9));
  DS_B(0x0791) = (uint8_t)rnd();
  DS_W(0x0792) = (uint16_t)rnd();
  DS_B(0x0794) = (uint8_t)rnd();
  DS_B(0x0795) = (uint8_t)rnd();
  DS_B(0x0796) = (uint8_t)rnd();
  {
    const uint16_t wins[] = {g_fwin, g_fpop, g_ficon, g_zoom, g_fkid[0], g_fwin};
    DS_W(0x2806) = wins[rn(6)];
  }
  static const uint16_t rects[] = {0x1694, 0x16ca, 0x1700, 0x165e};
  for (unsigned r = 0; r < 4; r++)
    for (unsigned k = 0; k < 4; k++) DS_W((uint16_t)(rects[r] + 2 * k)) = (uint16_t)(rn(5) == 0 ? rnd() : rn(800));
  static const uint16_t nums[] = {0x255e, 0x2654, 0x2656, 0x2658, 0x265a, 0x265c, 0x265e, 0x4b78, 0x0ab6, 0x055e, 0x54ec,
                                  0x538e, 0x4c76, 0x0560, 0x04c8, 0x0082, 0x164e, 0x1684, 0x16ba, 0x16f0, 0x27f4, 0x15c2,
                                  0x1658, 0x165a, 0x168e, 0x1690, 0x16c4, 0x16c6, 0x16fa, 0x16fc};
  for (unsigned k = 0; k < sizeof nums / sizeof nums[0]; k++) DS_W(nums[k]) = (uint16_t)(rn(3) == 0 ? rnd() : rn(300));
  DS_W(0x014c) = (uint16_t)(rn(4) == 0 ? 0xffff : rn(16));
  DS_W(0x0162) = (uint16_t)(rn(2) ? 0 : 1 + rn(5));
  ds_w32(0x0070, rnd());
  DS_B(0x0576) = (uint8_t)(rn(10) == 0 ? rnd() : rn(8));
  strcpy((char *)&DS_B(0x5764), rn(3) ? "C:\\STARS\\GAME.HST" : "g");
}
static void t_506c(void) {
  fw_windows();
  for (unsigned it = 0; it < 150; it++) {
    g_rng = 0x506c0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    ini_world();
    DIFF(4, 0x506c, FAR_CDECL);
  }
}

/* ---- 4:0x60f6 TITLEWNDPROC ---- */
/* The title window: WM_CREATE makes four real BUTTON windows; the banner bitmap and its palette are the
 * resource loaded by 8:0xad28 / 0xa910 (original code, called once before the DIFFs); the double of 15:0x6ab2 and
 * 31:0x5f14 (WM_COMMAND), 4:0x3b66 / 0x379a / 0x506c / 3:0x0c7a run as C in the C version. */
#define TW(hw, m, wp, lphi, lplo) DIFF(4, 0x60f6, FAR_PASCAL, (hw), (m), (wp), (lphi), (lplo))
static void tw_world(uint16_t dib, uint16_t dibpal, uint16_t icon, uint32_t strs) {
  stubs_clear_all();
  input_reset();
  fw_state();
  DS_W(0x54f6) = (uint16_t)(rn(3) == 0 ? 4 + rn(4) : rn(4) == 0 ? 0xfff0 : 8 + rn(2) * 8);
  DS_B(0x0791) = (uint8_t)rnd();
  DS_B(0x0792) = (uint8_t)rnd();
  DS_W(0x0366) = (uint16_t)rnd();
  DS_W(0x267a) = (uint16_t)(rn(8) == 0 ? rnd() : 8 + rn(24));
  DS_W(0x2930) = GetStockObject(0);
  DS_W(0x03dc) = rn(3) ? dib : 0;
  DS_W(0x03da) = rn(5) == 0 ? 0 : rn(2) ? dibpal : g_fpal;
  DS_W(0x0316) = icon;
  DS_W(0x0010) = GetStockObject(rn(2));
  DS_W(0x4a5c) = GetStockObject(13);
  DS_W(0x03de) = g_fkid[rn(5)];
  DS_W(0x0314) = (uint16_t)rn(2);
  DS_W(0x0312) = rn(2) ? g_fkid[4] : 0;
  DS_W(0x2806) = g_fwin;
  ds_w32(0x00b6, rn(2) ? 0 : rnd());
  ds_w32(0x0070, rn(2) ? 0 : rnd());
  DS_B(0x0080) = (uint8_t)rnd();
  DS_B(0x251a) = (uint8_t)(rnd() & 0xf3);
  memset(&DS_B(0x5764), 0, 0x40);
  if (rn(3)) strcpy((char *)&DS_B(0x5764), rn(3) ? "C:\\STARS\\GAME.HST" : "C:\\STARS\\NOFILE.HST");
  ds_w32(0x05f6, strs);
}
static void t_60f6(void) {
  fw_windows();
  CHECK_EQ(vfs_put("C:\\STARS\\GAME.HST", "h", 1), 0);
  uint32_t strs = gblock(0x900);
  for (unsigned k = 0; k < 9; k++) snprintf((char *)LIN(strs) + 0x100 * k, 0x20, "GAME%u.HST", k);
  uint16_t dib = 0, dibpal = 0;
  DS_W(0x54f4) = MEM_SEL_DGROUP;
  {
    const uint16_t a[1] = {0x01c1};
    dib = (uint16_t)harness_call_far(MK_FP(SEG_SEL(8), 0xad28), FAR_CDECL, a, 1);
    if (dib) {
      const uint16_t b[1] = {dib};
      dibpal = (uint16_t)harness_call_far(MK_FP(SEG_SEL(8), 0xa910), FAR_CDECL, b, 1);
    }
  }
  CHECK(dib != 0);
  CHECK(dibpal != 0);
  const uint16_t icon = LoadIcon(0, 0x00007f00u);
  uint16_t tw = mkwin(WS_OVERLAPPED | WS_VISIBLE, 0, 0, 640, 480, 0, 0);
  uint16_t twi = mkwin(WS_OVERLAPPED | WS_VISIBLE | WS_MINIMIZE, 30, 40, 100, 80, 0, 0);
  static const struct { uint16_t msg; unsigned rounds; } plan[] = {
      {1, 40}, {2, 30}, {0xf, 70}, {0x111, 120}, {0x30f, 14}, {0x311, 14}, {0x10, 3}, {0x14, 3}, {0x5, 3}, {0x200, 3},
  };
  for (unsigned pi = 0; pi < sizeof plan / sizeof plan[0]; pi++) {
    const uint16_t m = plan[pi].msg;
    for (unsigned it = 0; it < plan[pi].rounds; it++) {
      g_rng = 0x60f60001u + 0x9e3779b9u * (pi * 1000 + it + 1);
      tw_world(dib, dibpal, icon, strs);
      uint16_t hw = tw, wp = 0;
      uint32_t lp = rn(2) ? 0 : rnd();
      if (m == 1) {
        static const uint16_t ws[] = {300, 800, 1000, 1300, 1800}, hs[] = {300, 480, 520, 640, 700};
        hw = mkwin(WS_OVERLAPPED | WS_VISIBLE, 0, 0, ws[rn(5)], hs[rn(5)], 0, 0);
        if (rn(3) == 0) DS_W(0x03da) = 0;
      } else if (m == 2) {
        if (rn(2)) {
          uint16_t gh = GlobalAlloc(0x42, 64);
          (void)GlobalLock(gh);
          DS_W(0x03dc) = gh;
        } else {
          DS_W(0x03dc) = 0;
        }
        ini_world();
        DS_W(0x2806) = g_fwin;
        DS_W(0x03de) = g_fkid[0];
      } else if (m == 0xf) {
        hw = rn(4) == 0 ? twi : rn(2) ? g_fwin : tw;
        if (rn(4) == 0) {
          DS_W(0x03dc) = 0;
        }
        if (DS_W(0x03dc) == 0 && rn(2)) DS_W(0x0316) = 0;
        ds_w32(0x0872, rnd());
      } else if (m == 0x111) {
        static const uint16_t ids[] = {0, 1, 2, 3, 4, 0x1234, 1, 2, 0};
        wp = ids[rn(9)];
        if (wp == 1 || wp == 2) {
          stubs_clear_all();
          b66_world(g_fwin, g_fkid);
          ds_w32(0x05f6, strs);
          DS_B(0x0080) = (uint8_t)rnd();
          DS_W(0x0314) = (uint16_t)rn(2);
          DS_W(0x0312) = rn(2) ? g_fkid[4] : 0;
          DS_B(0x251a) = (uint8_t)((DS_B(0x251a) & ~8u) | (rn(2) ? 8u : 0u));
          static const uint16_t quitv[] = {8, 0x0108, 0x18};
          uint16_t qv = quitv[rn(3)];
          stub_eff_ds(S_8_B8F6, (int)rn(3), 0x251a, &qv, 2);
          DS_B(0x0793) = (uint8_t)rnd();
        } else if (wp == 3) {
          ini_world();
          DS_W(0x2806) = g_fwin;
        }
      } else if (m == 0x311) {
        if (rn(3) == 0) wp = hw;
        else wp = rn(2) ? twi : 0;
      }
      TW(hw, m, wp, HI(lp), LO(lp));
    }
  }
}

/* The module itself: the dialog templates and bitmaps are resources of the exe (kernel_load). */
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
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}

int main(void) {
  g_only = getenv("SEG04_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  load_module();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  commdlg_register();
  toolhelp_win87em_register();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* LocalAlloc (41:0x41f8, the dialogs) */
  register_test_class();
  stubs_init();
  pf_init();
  commdlg_warm();
  if (want("0000")) t_0000();
  for (unsigned i = 0; i < 11; i++) (void)reg_game_class(g_class_names[i]); /* the game's classes, test procedure */
  if (want("4db8")) t_4db8();
  if (want("3b30")) t_3b30();
  if (want("place")) t_placement();
  if (want("18b0")) t_18b0();
  if (want("4580")) t_4580();
  if (want("1e5e")) t_1e5e();
  if (want("197a")) t_197a();
  if (want("coder")) t_coder();
  if (want("39f8")) t_39f8();
  if (want("3ecc")) t_3ecc();
  if (want("5d98")) t_5d98();
  if (want("143a")) t_143a();
  if (want("379a")) t_379a();
  if (want("467c")) t_467c();
  if (want("41f8")) t_41f8();
  if (want("4cec")) t_4cec();
  if (want("1f40") || want("fc")) t_1f40();
  if (want("0528") || want("fw")) t_0528();
  if (want("506c")) t_506c();
  if (want("60f6")) t_60f6();
  if (want("02ca")) t_02ca();
  if (want("4dba")) t_4dba();
  if (want("4090")) t_4090();
  if (want("3b66")) t_3b66();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
