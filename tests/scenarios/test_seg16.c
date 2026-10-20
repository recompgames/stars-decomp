/* Diff tests for decompiled segment 16 (src/seg16.c). Local-only (docs/PLAN.md decision 10).
 *
 * Segment 16 holds three dialog procedures and the fleet-waypoint / ship-loss code:
 *   16:0x0000 ZIPORDERDLG    the ZIP order dialog procedure (four radio buttons 0x431 to 0x434)
 *   16:0x0606                enable the "rename" and "reset" buttons of ZIPORDERDLG for a preset
 *   16:0x063e RENAMEZIPDLG   the dialog procedure of the preset-name dialog
 *   16:0x0794 RENAMEDLG      the dialog procedure of the other name dialog
 * (the later groups are described where they are tested).
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered Win16 API call log, which here also holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. See the notes of each function in
 * docs/functions.csv for which memory a watch hashes and which it does not. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/harness.h"
#include "harness/gdi_thunks.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
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
static void register_test_class(void) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr("SEG16TEST");
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG16TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy,
                            parent, id, 0x1234, 0);
  CHECK(h != 0);
  g_class_ctr++;
  return h;
}

/* A control of a predefined class (the test class answers every message with 1, so it has no text). */
static uint16_t mkctl(const char *cls, uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr(cls), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
  CHECK(h != 0);
  return h;
}

/* ---- test doubles: logged thunks (rule j2) ---- */

/* A double replaces the first 5 bytes of a callee in the code segment image with JMP FAR to an import
 * thunk of the module "STUBS" (CDECL: the caller pops the arguments), so the same bytes serve the ORIGINAL
 * run and the C version's CALL_ORIG. Every call is an entry of the API log (module, id, the argument
 * words as pushed, the return value), in order with the real API calls and the other doubles. What the
 * log does not show is the memory a pointer argument points to: the handler therefore hashes, at every
 * call, the memory its configuration names (a fleet record and its waypoint list, near buffers of the
 * stack) into a log in the arena, which DIFF compares as arena memory. The return value is the next
 * value queued for the id (stub_push; the queue index lives in the arena and is rewound with every
 * snapshot restore), else the default (stub_def). The functions under test are never doubled. */
#define STUB_BLKSZ 0x8000
#define STUB_DEF 0x10     /* default return (ax, dx) of id: 4 bytes each */
#define STUB_QLEN 0x200   /* queue length of id (word) */
#define STUB_QIDX 0x280   /* queue index of id (word) */
#define STUB_Q 0x400      /* queue of id: 64 entries of 4 bytes (ax, dx) */
#define STUB_QMAX 64
#define STUB_LOG0 0x4000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs;     /* argument words */
  int8_t farw;       /* argument word index of a far pointer to a fleet record: its 0x7c bytes and its waypoint list are hashed, -1 none */
  int8_t ssw1, ssw2; /* argument word index of a near pointer (SS) whose 0x7c bytes are hashed, -1 none */
} StubCfg;
enum { S_31_61B0, S_31_AB72, S_10_6C1C, S_10_6230, S_10_678C, S_10_6312, S_7_14A8, S_7_1A70, S_NSTUB };
static const StubCfg g_stubs[S_NSTUB] = {
    [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, -1, -1, -1},
    [S_31_AB72] = {"31:ab72", 31, 0xab72, 0, -1, -1, -1},
    [S_10_6C1C] = {"10:6c1c", 10, 0x6c1c, 2, -1, 0, 1},
    [S_10_6230] = {"10:6230", 10, 0x6230, 4, 0, -1, -1},
    [S_10_678C] = {"10:678c", 10, 0x678c, 3, 0, -1, -1},
    [S_10_6312] = {"10:6312", 10, 0x6312, 7, 0, -1, -1},
    [S_7_14A8] = {"7:14a8", 7, 0x14a8, 1, -1, -1, -1},
    [S_7_1A70] = {"7:1a70", 7, 0x1a70, 1, -1, -1, -1},
};

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint16_t qlen, qidx;
  memcpy(&qlen, blk + STUB_QLEN + 2 * c->ordinal, 2);
  memcpy(&qidx, blk + STUB_QIDX + 2 * c->ordinal, 2);
  uint32_t h = 2166136261u;
  if (cfg->farw >= 0) {
    uint32_t fp = (uint32_t)hcall_word(c, (unsigned)cfg->farw) | ((uint32_t)hcall_word(c, (unsigned)cfg->farw + 1) << 16);
    if (fp != 0 && mem_check(fp, 0x7c)) {
      h = fnv(h, LIN(fp), 0x7c);
      uint32_t wp;
      uint16_t n;
      memcpy(&wp, (uint8_t *)LIN(fp) + 0x64, 4);
      memcpy(&n, (uint8_t *)LIN(fp) + 0x62, 2);
      if (wp != 0 && n < 14 && mem_check(wp, (uint16_t)(4 + 0x12 * n))) h = fnv(h, LIN(wp), (size_t)(4 + 0x12 * n));
    }
  }
  const int8_t ss[2] = {cfg->ssw1, cfg->ssw2};
  for (int k = 0; k < 2; k++) {
    if (ss[k] >= 0) {
      uint16_t p = hcall_word(c, (unsigned)ss[k]);
      h = fnv(h, LIN(MK_FP(c->regs->ss, p)), 0x7c);
    }
  }
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
  if (qidx < qlen) {
    memcpy(&c->ret, blk + STUB_Q + (size_t)c->ordinal * STUB_QMAX * 4 + (size_t)qidx * 4, 4);
    qidx++;
    memcpy(blk + STUB_QIDX + 2 * c->ordinal, &qidx, 2);
  } else {
    memcpy(&c->ret, blk + STUB_DEF + 4 * c->ordinal, 4);
  }
  if (c->ordinal == S_10_6230) {
    /* the real 10:0x6230 drops waypoints: the double only models that, by storing the low word of the
     * queued value (when it is not 0xffff) as the fleet's waypoint count (+0x62) */
    uint32_t fp = (uint32_t)hcall_word(c, 0) | ((uint32_t)hcall_word(c, 1) << 16);
    if (fp != 0 && (c->ret & 0xffff) != 0xffff && mem_check(fp, 0x7c)) memcpy((uint8_t *)LIN(fp) + 0x62, &c->ret, 2);
  }
}
static void stub_def(unsigned id, uint16_t ax, uint16_t dx) {
  uint32_t v = ((uint32_t)dx << 16) | ax;
  memcpy((uint8_t *)LIN(g_stub_blk) + STUB_DEF + 4 * id, &v, 4);
}
/* Forgets the queues and the default of every double and rewinds the hash log. */
static void stubs_clear_all(void) {
  uint8_t *blk = LIN(g_stub_blk);
  for (unsigned id = 0; id < S_NSTUB; id++) {
    memset(blk + STUB_QLEN + 2 * id, 0, 2);
    memset(blk + STUB_QIDX + 2 * id, 0, 2);
    stub_def(id, id == S_10_6230 ? 0xffff : 0, 0);
  }
  uint16_t at = STUB_LOG0;
  memcpy(blk, &at, 2);
}
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
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
  }
  stubs_clear_all();
}

/* ---- the ZIP order dialog (16:0x0000) and its helpers ---- */

/* A dialog window with the controls the procedures address. */
static uint16_t g_dlg, g_c431, g_c432, g_c433, g_c434, g_c417, g_c41b, g_c817, g_c10c;
static void make_zip_dialog(void) {
  g_dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 400, 300, 0, 0);
  g_c431 = mkwin(WS_CHILD | WS_VISIBLE, 5, 5, 180, 30, g_dlg, 0x431);
  g_c432 = mkwin(WS_CHILD | WS_VISIBLE, 5, 40, 180, 30, g_dlg, 0x432);
  g_c433 = mkwin(WS_CHILD | WS_VISIBLE, 5, 75, 180, 30, g_dlg, 0x433);
  g_c434 = mkwin(WS_CHILD | WS_VISIBLE, 5, 110, 180, 30, g_dlg, 0x434);
  g_c417 = mkwin(WS_CHILD | WS_VISIBLE, 200, 5, 80, 20, g_dlg, 0x417);
  g_c41b = mkwin(WS_CHILD | WS_VISIBLE, 200, 30, 80, 20, g_dlg, 0x41b);
  g_c817 = mkwin(WS_CHILD | WS_VISIBLE, 200, 55, 80, 20, g_dlg, 0x817);
  g_c10c = mkctl("EDIT", WS_CHILD | WS_VISIBLE, 200, 80, 80, 20, g_dlg, 0x10c);
}
/* The four presets of DS:0x5448, stride 0x18: five words, a 13-byte name at +0x0a, a flag at +0x17. */
static void set_presets(unsigned flags) {
  for (unsigned i = 0; i < 4; i++) {
    uint16_t a = (uint16_t)(0x5448 + 0x18 * i);
    for (unsigned k = 0; k < 5; k++) DS_W(a + 2 * k) = (uint16_t)(((k + i * 3) & 15) << 12 | (0x123 + 0x111 * k + i));
    char nm[16];
    snprintf(nm, sizeof nm, "Pre&set %u", i);
    memset(&DS_B(a + 0x0a), 0, 13);
    memcpy(&DS_B(a + 0x0a), nm, strlen(nm));
    DS_B(a + 0x17) = (uint8_t)((flags >> (2 * i)) & 3);
  }
}
static void zip_state(void) {
  DS_W(0x0010) = GetStockObject(0); /* WHITE_BRUSH */
  ds_w32(0x253a, 0x00C0C0C0);
  DS_W(0x267a) = 14;
  DS_W(0x2930) = GetStockObject(16);
  DS_W(0x292e) = GetStockObject(16); /* the font 8:0xa42e measures names with */
  DS_W(0x54f4) = 0;
  DS_W(0x18ae) = 0;
  DS_W(0xc4a) = 0;
  DS_B(0x791) = 0;
  DS_B(0x794) = 0;
}

/* ---- 16:0x0606 ---- */
static void t_0606(void) {
  make_zip_dialog();
  for (unsigned fl = 0; fl < 4; fl++) {
    set_presets(fl == 0 ? 0x00 : fl == 1 ? 0xff : fl == 2 ? 0x55 : 0xaa);
    for (unsigned idx = 0; idx < 4; idx++) {
      DIFF(16, 0x0606, FAR_CDECL, g_dlg, idx);
    }
  }
  DS_B(0x545f) = 0x80;
  DIFF(16, 0x0606, FAR_CDECL, g_dlg, 0);
  DIFF(16, 0x0606, FAR_CDECL, 0, 1);       /* no dialog: GetDlgItem gives 0 */
  DIFF(16, 0x0606, FAR_CDECL, g_c431, 1);  /* a window without the two buttons */
  DestroyWindow(g_dlg);
}

/* ---- 16:0x0000 ZIPORDERDLG ---- */

/* The far block 16:0x0000 copies ten bytes from when the preset is renamed: DS:0x4bf0 is a far
 * pointer to an array of 0x12-byte records, DS:0x4b7a the record, the ten bytes start at +0xc. */
static uint32_t g_src;
static void zip_src_setup(void) {
  g_src = gblock(0x400);
  ds_w32(0x4bf0, g_src);
  DS_W(0x4b7a) = 3;
  for (unsigned i = 0; i < 0x80; i++) *(uint8_t *)LIN(MK_FP(HI(g_src), (uint16_t)(LO(g_src) + 0x30 + i))) = (uint8_t)(0xA0 + i);
}

static void t_0000(void) {
  make_zip_dialog();
  zip_state();
  zip_src_setup();
  /* WM_PAINT (0x0f): a preset that is not custom (flag 0) and the five-row custom one (flag != 0);
   * the first word of each row picks the string 0x6d + (word >> 12), so the nibbles also vary. */
  for (unsigned fl = 0; fl < 4; fl++) {
    set_presets(fl == 0 ? 0x00 : fl == 1 ? 0xff : fl == 2 ? 0x55 : 0xaa);
    for (unsigned idx = 0; idx < 4; idx++) {
      DS_W(0x51b2) = (uint16_t)idx;
      DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x000f, 0, 0, 0);
    }
  }
  /* every string 0x6d to 0x7c in the rows */
  set_presets(0xff);
  for (unsigned n = 0; n < 16; n++) {
    for (unsigned k = 0; k < 5; k++) DS_W(0x5448 + 2 * k) = (uint16_t)((((n + k) & 15) << 12) | (0x0abc + k));
    DS_W(0x51b2) = 0;
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x000f, 0, 0, 0);
  }
  /* WM_ERASEBKGND (0x14) */
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0014, GetStockObject(0), 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, 0, 0x0014, 0, 0x1234, 0x5678);
  /* WM_CTLCOLOR (0x19): the control is looked up among 0x431 to 0x434 */
  const uint16_t dcs = GetStockObject(0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, g_c431);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 5, g_c432);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 0, g_c433);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, g_c434);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, g_c417);  /* not one of the four */
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, 0);
  /* WM_INITDIALOG (0x110): names (flag set) and generated names (flag clear), with and without the
   * flag bit 8 of DS:0x791 that calls 31:0x61b0 */
  for (unsigned fl = 0; fl < 4; fl++) {
    set_presets(fl == 0 ? 0x00 : fl == 1 ? 0xff : fl == 2 ? 0x55 : 0xaa);
    for (unsigned b = 0; b < 2; b++) {
      DS_B(0x791) = (uint8_t)(b ? 0x08 : 0x00);
      DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0110, 0, 0, 0);
    }
  }
  DS_B(0x791) = 0xf7;
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0110, 0, 0, 0);
  DS_B(0x791) = 0;
  /* a name with the '&' sign doubled and a long name */
  set_presets(0xff);
  memcpy(&DS_B(0x5452), "a&&b&", 6);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0110, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, 0, 0x0110, 0, 0, 0);
  /* WM_COMMAND (0x111): the four radio buttons, with a notification code, and ids 1 and 2 */
  set_presets(0x55);
  for (unsigned id = 0x430; id <= 0x435; id++) {
    DS_W(0x51b2) = 3;
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, id, 0, 0);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, id, 0, 1);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, id, 0x1234, 0);
  }
  for (unsigned b = 0; b < 2; b++) {
    DS_B(0x791) = (uint8_t)(b ? 0x08 : 0x00);
    DS_W(0x18ae) = 0x4321;
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 1, 0, 0);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 2, 0, 0);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 1, 5, 0);
  }
  DS_B(0x791) = 0;
  /* the help button, the two buttons that reset and rename a preset, and other ids */
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 0x76, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 0x77, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 0, 0, 0);
  for (unsigned idx = 0; idx < 4; idx++) {
    DS_W(0x51b2) = (uint16_t)idx;
    set_presets(0xff);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 0x817, 0, 0);
    set_presets(0x00);
    DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, 0x817, 0, 0);
  }
  /* rename (0x816 and 0x41b): the dialog box 0x7e3 is never answered (DS:0x54f4 = the instance, the
   * message queue is empty), so DialogBox gives 0; with a bad instance it fails with -1 and the new
   * name (the buffer DS:0x5866) is taken */
  const uint16_t ids2[] = {0x816, 0x41b};
  for (unsigned hi = 0; hi < 2; hi++) {
    for (unsigned id = 0; id < 2; id++) {
      for (unsigned fl = 0; fl < 2; fl++) {
        for (unsigned idx = 0; idx < 4; idx += 3) {
          DS_W(0x54f4) = hi ? MEM_SEL_DGROUP : 0;
          DS_W(0x51b2) = (uint16_t)idx;
          set_presets(fl ? 0xff : 0x00);
          memset(&DS_B(0x5866), 0, 4);
          DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, ids2[id], 0, 0);
          memcpy(&DS_B(0x5866), "new&name", 9);
          DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, ids2[id], 0, 0);
        }
      }
    }
  }
  /* the dialog answered with Return (its default button): OK with the edit text as it was, here an empty name */
  for (unsigned id = 0; id < 2; id++) {
    for (unsigned fl = 0; fl < 2; fl++) {
      DS_W(0x54f4) = MEM_SEL_DGROUP;
      DS_W(0x51b2) = 2;
      set_presets(fl ? 0xff : 0x00);
      memset(&DS_B(0x5452 + 0x18 * 2), 0, 13);          /* an empty name for the preset */
      rt_input_key(VK_RETURN, 0x1c, 1, 0);
      rt_input_key(VK_RETURN, 0x1c, 0, 0);
      DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0111, ids2[id], 0, 0);
    }
  }
  DS_W(0x54f4) = 0;
  /* other messages */
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0000, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0112, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0010, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0015, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x0018, 0, 0, 0);
  DIFF(16, 0x0000, FAR_PASCAL, g_dlg, 0x011f, 0, 0, 0);
  DestroyWindow(g_dlg);
}

/* ---- 16:0x063e RENAMEZIPDLG and 16:0x0794 RENAMEDLG ---- */

static void t_name_dialogs(void) {
  zip_state();
  g_dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 200, 0, 0);
  g_c10c = mkctl("EDIT", WS_CHILD | WS_VISIBLE, 5, 5, 200, 20, g_dlg, 0x10c);
  const uint16_t dcs = GetStockObject(0);
  /* tools/lint_src.py looks for DIFF(seg, 0xOFF, ...) with a literal offset: these two are the first cases of the loop below,
   * which repeats them for both procedures with the offset in a variable */
  DIFF(16, 0x063e, FAR_PASCAL, g_dlg, 0x0014, dcs, 0, 0);
  DIFF(16, 0x0794, FAR_PASCAL, g_dlg, 0x0014, dcs, 0, 0);
  const uint16_t procs[] = {0x063e, 0x0794};
  for (unsigned p = 0; p < 2; p++) {
    const uint16_t off = procs[p];
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0014, dcs, 0, 0);
    DIFF(16, off, FAR_PASCAL, 0, 0x0014, 0, 0x1234, 0x5678);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0019, dcs, 0, 6);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0019, dcs, 6, 5);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0019, dcs, 7, 6);
    for (unsigned st = 0; st < 2; st++) {
      DS_W(0x18ae) = (uint16_t)st;
      memcpy(&DS_B(0x5866), "Name 1", 7);
      DS_W(0x08a8) = 100;
      DS_W(0x08aa) = 50;
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0110, 0, 0, 0);
      DIFF(16, off, FAR_PASCAL, 0, 0x0110, 0, 0, 0);
    }
    /* WM_COMMAND */
    for (unsigned st = 0; st < 3; st++) {
      DS_W(0x18ae) = (uint16_t)st;
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 1, 0, 0);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 2, 0, 0);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x76, 0, 0);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x77, 0, 0);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0, 0, 0);
      DIFF(16, off, FAR_PASCAL, 0, 0x0111, 1, 0, 0);
    }
    /* the edit control: EN_UPDATE (0x400) */
    SetWindowText(g_c10c, fstr("short"));
    for (unsigned busy = 0; busy < 2; busy++) {
      DS_W(0x0c4a) = (uint16_t)busy;
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x10c, 0x400, g_c10c);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x10c, 0x300, g_c10c);
      DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x10d, 0x400, g_c10c);
    }
    DS_W(0x0c4a) = 0;
    SetWindowText(g_c10c, fstr("a very long name that surely does not fit the field of the dialog box at all, wide W W W W W W W W W W"));
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 0x10c, 0x400, g_c10c);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0111, 1, 0, 0); /* OK with a name longer than the field (cut at 0xa0 pixels / 0x20 characters) */
    /* other messages */
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0000, 0, 0, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0112, 0, 0, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0013, 0, 0, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0015, 0, 0, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x0018, 0, 6, 0);
    DIFF(16, off, FAR_PASCAL, g_dlg, 0x011f, 0, 0, 0);
  }
  DestroyWindow(g_dlg);
}

/* ---- the fleet world ---- */

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }
static bool chance(uint32_t num, uint32_t den) { return rn(den) < num; }

/* The message buffer 6:0x7344 appends to (it runs for real): a zeroed block of 0x1000 bytes at DS:0xaac/0xaae, empty
 * (length DS:0xab0, count DS:0xab4). The "word +6" of the player rows (DS:0x5a6a + 0xc0 * k) decides whether a
 * player gets messages (6:0x7410: bit 9 set and bits 13-15 not all set mutes it), so it takes all three shapes. */
static uint32_t g_msgbuf;
static void msg_state(void) {
  if (!g_msgbuf) {
    g_msgbuf = gblock(0x1000);
    CHECK_EQ(LO(g_msgbuf), 0);
  }
  memset(LIN(g_msgbuf), 0, 0x1000);
  DS_W(0x0aac) = LO(g_msgbuf);
  DS_W(0x0aae) = HI(g_msgbuf);
  DS_W(0x0ab0) = 0;
  DS_W(0x0ab4) = 0;
  for (unsigned k = 0; k < 16; k++) {
    uint16_t w = (uint16_t)rnd();
    switch (rn(4)) {
      case 0: w = (uint16_t)(w & ~0x0200u); break;
      case 1: w = (uint16_t)((w | 0x0200u) & ~0xe000u); break;
      case 2: w = (uint16_t)(w | 0xe200u); break;
      default: break;
    }
    memcpy(LINN((uint16_t)(0x5a6a + 0xc0 * k)), &w, 2);
  }
}

static uint8_t rb_(uint32_t fp, uint16_t off) { return *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))); }
static uint16_t rw(uint32_t fp, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), 2);
  return v;
}
static void ww(uint32_t fp, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 2); }
static void wd(uint32_t fp, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))), &v, 4); }
static void wb(uint32_t fp, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(HI(fp), (uint16_t)(LO(fp) + off))) = v; }
static void fill_random(uint32_t fp, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) wb(fp, i, (uint8_t)rnd());
}

#define NPL 4       /* players with a design table */
#define NFL 6       /* fleets in the fleet table */
#define WPSZ (4 + 0x12 * 12)
static uint32_t g_dtab[NPL];  /* ShipDesign[16] per player (DS:0xBE + 4 * owner) */
static uint32_t g_fl[NFL];    /* the fleet records */
static uint32_t g_wp[NFL];    /* their waypoint lists */
static uint32_t g_ftab;       /* the far pointers to the fleets (DS:0xBA) */
static uint32_t g_ptab;       /* the planet xy dwords (DS:0x31BA) */
static void world_alloc(void) {
  for (unsigned p = 0; p < NPL; p++) {
    g_dtab[p] = gblock(0x8d0);
    ds_w32((uint16_t)(0xbe + 4 * p), g_dtab[p]);
  }
  g_ftab = gblock(4 * NFL + 4);
  ds_w32(0x00ba, g_ftab);
  for (unsigned i = 0; i < NFL; i++) {
    g_fl[i] = gblock(0x7c);
    g_wp[i] = gblock(WPSZ);
    wd(g_ftab, (uint16_t)(4 * i), g_fl[i]);
  }
  DS_W(0x553a) = NFL;
  g_ptab = gblock(4 * 64);
  /* DS:0x31BA is a table inside DGROUP: the planet positions live there, not in a block */
  DS_W(0x007a) = 0;
}
/* A fleet with random contents and a consistent header: owner, waypoint list pointer and count. */
static void fleet_random(unsigned i, unsigned owner, unsigned nwp) {
  fill_random(g_fl[i], 0x7c);
  fill_random(g_wp[i], WPSZ);
  ww(g_fl[i], 0, (uint16_t)(i + 1));
  ww(g_fl[i], 2, (uint16_t)owner);
  ww(g_fl[i], 0x62, (uint16_t)nwp);
  wd(g_fl[i], 0x64, g_wp[i]);
}
static void designs_random(void) {
  for (unsigned p = 0; p < NPL; p++) fill_random(g_dtab[p], 0x8d0);
}

/* ---- 16:0x16f6 and 16:0x175c: does the fleet hold a ship of a design with a given hull ---- */

static void t_designs_mask(void) {
  world_alloc();
  designs_random();
  uint32_t f = g_fl[0];
  fleet_random(0, 1, 0);
  /* each hull number (the byte at design +0) alone, in each slot, with a non-empty count there */
  for (unsigned hull = 0; hull < 256; hull += (hull < 70 ? 1 : 7)) {
    for (unsigned slot = 0; slot < 16; slot += 5) {
      for (unsigned i = 0; i < 16; i++) {
        ww(f, (uint16_t)(0x0c + 2 * i), (uint16_t)(i == slot ? 3 : 0));
        wb(g_dtab[1], (uint16_t)(0x8d * i), (uint8_t)(i == slot ? hull : 0));
      }
      DIFF(16, 0x16f6, FAR_CDECL, LO(f), HI(f));
      DIFF(16, 0x175c, FAR_CDECL, LO(f), HI(f));
    }
  }
  /* a design with the hull but a count of zero does not count */
  for (unsigned i = 0; i < 16; i++) {
    ww(f, (uint16_t)(0x0c + 2 * i), 0);
    wb(g_dtab[1], (uint16_t)(0x8d * i), 14);
  }
  DIFF(16, 0x16f6, FAR_CDECL, LO(f), HI(f));
  DIFF(16, 0x175c, FAR_CDECL, LO(f), HI(f));
  for (unsigned i = 0; i < 16; i++) wb(g_dtab[1], (uint16_t)(0x8d * i), 5);
  DIFF(16, 0x175c, FAR_CDECL, LO(f), HI(f));
  /* random fleets and designs, any owner of the table */
  for (unsigned it = 0; it < 300; it++) {
    designs_random();
    fleet_random(0, rn(NPL), 0);
    for (unsigned i = 0; i < 16; i++) {
      ww(f, (uint16_t)(0x0c + 2 * i), chance(1, 3) ? (uint16_t)rnd() : 0);
      if (chance(1, 2)) wb(g_dtab[rw(f, 2)], (uint16_t)(0x8d * i), (uint8_t)(chance(1, 2) ? 4 + rn(3) : 12 + rn(5)));
    }
    DIFF(16, 0x16f6, FAR_CDECL, LO(f), HI(f));
    DIFF(16, 0x175c, FAR_CDECL, LO(f), HI(f));
  }
}

/* ---- 16:0x133e: the fleets that follow a fleet get its position ---- */

static void t_133e(void) {
  world_alloc();
  for (unsigned it = 0; it < 300; it++) {
    for (unsigned i = 0; i < NFL; i++) {
      fleet_random(i, rn(3), rn(9));
      /* waypoints: a mix of types (low nibble of +7; 2 = follow a fleet) aimed at fleet ids 1 to NFL */
      for (unsigned k = 0; k < 12; k++) {
        uint16_t w = (uint16_t)(4 + 0x12 * k);
        wb(g_wp[i], (uint16_t)(w + 7), (uint8_t)(chance(1, 2) ? 2 : rn(16)) | (uint8_t)(rnd() & 0xf0));
        ww(g_wp[i], (uint16_t)(w + 4), (uint16_t)(1 + rn(NFL)));
      }
    }
    if (it % 7 == 6) wd(g_ftab, (uint16_t)(4 * rn(NFL)), 0);  /* a hole in the table ends the walk */
    if (it % 11 == 10) DS_W(0x553a) = (uint16_t)rn(3);
    else DS_W(0x553a) = NFL;
    uint32_t arg = it % 5 == 4 ? g_fl[0] + 0 : g_fl[rn(NFL)];
    DIFF(16, 0x133e, FAR_CDECL, LO(arg), HI(arg));
  }
  DS_W(0x553a) = NFL;
  for (unsigned i = 0; i < NFL; i++) wd(g_ftab, (uint16_t)(4 * i), g_fl[i]);
  DS_W(0x553a) = 0;
  DIFF(16, 0x133e, FAR_CDECL, LO(g_fl[0]), HI(g_fl[0]));
  DS_W(0x553a) = 0x8000;                       /* a negative count: the walk is skipped */
  DIFF(16, 0x133e, FAR_CDECL, LO(g_fl[0]), HI(g_fl[0]));
  DS_W(0x553a) = NFL;
}

/* ---- design and fleet values: 16:0x20c8, 0x1d1c, 0x1ca2, 0x193e, 0x1a34, 0x1aea ---- */

/* Random designs for the value functions. The component table 2:0x50f8 knows only some (category, item)
 * pairs and leaves a stale pointer for the others, so the generator emits, for the categories a
 * function hands to it, items it knows:
 *   kind 0: category 0x80 items 0 to 8 (16:0x193e looks up 0 to 6; 16:0x1a34 counts item 7)
 *   kind 1: category 0x100 items 0 to 9 and category 0x10 item 0x12 (16:0x1aea; 0x10 is not looked up)
 *   kind 2: category 0x10 items 0 to 15 (16:0x1d1c looks up every slot of category 0x10)
 * Other slots carry categories no function looks up. */
static const uint16_t noise_cats[] = {0, 1, 2, 4, 8, 0x20, 0x40, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x7fff, 0xffff};
static void design_random(uint32_t d, int kind) {
  fill_random(d, 0x8d);
  uint16_t hull = (uint16_t)(chance(1, 6) ? (chance(1, 2) ? 0x1b : 0x1c) : rn(0x44));
  if (chance(1, 20)) hull = (uint16_t)(0xffe0 + rn(0x20));  /* negative: below 0x20 as a signed word */
  ww(d, 0, hull);
  unsigned n = rn(5) == 0 ? 16 : rn(9);
  wb(d, 0x7a, (uint8_t)n);
  for (unsigned k = 0; k < 16; k++) {
    uint16_t cat, item;
    unsigned r = rn(5);
    if (r < 3 || kind == 2) {
      if (kind == 0) { cat = 0x80; item = (uint16_t)rn(9); }
      else if (kind == 1) { cat = chance(1, 4) ? 0x10 : 0x100; item = cat == 0x10 ? 0x12 : (uint16_t)rn(10); }
      else { cat = chance(3, 4) ? 0x10 : noise_cats[rn(15)]; item = (uint16_t)rn(16); if (cat != 0x10) item = (uint16_t)rnd(); }
    } else {
      cat = noise_cats[rn(15)];
      item = (uint16_t)rnd();
      if (cat == 0x10 || cat == 0x80 || cat == 0x100) cat = 0x2000;
    }
    ww(d, (uint16_t)(0x3a + 4 * k), cat);
    wb(d, (uint16_t)(0x3a + 4 * k + 2), (uint8_t)item);
    wb(d, (uint16_t)(0x3a + 4 * k + 3), (uint8_t)(chance(1, 5) ? 0 : chance(1, 2) ? 1 + rn(3) : rnd()));
  }
}
static uint32_t rd_ds32(uint16_t off) { uint32_t v; memcpy(&v, LINN(off), 4); return v; }
static uint32_t rd_fl_xy(uint32_t f) { return (uint32_t)rw(f, 8) | ((uint32_t)rw(f, 0xa) << 16); }
static void fleet_counts(uint32_t f) {
  for (unsigned i = 0; i < 16; i++) {
    uint16_t c = chance(1, 3) ? 0 : (uint16_t)(chance(1, 8) ? rnd() : 1 + rn(60));
    if (chance(1, 40)) c = (uint16_t)(0x8000 + rn(4));
    ww(f, (uint16_t)(0x0c + 2 * i), c);
  }
}

static void t_slot_value(void) {
  uint32_t slot = gblock(8);
  static const uint16_t cats[] = {0, 1, 2, 3, 4, 5, 8, 9, 0x10, 0x11, 0x20, 0x40, 0x80, 0x81, 0x100, 0x400, 0x800, 0x801, 0x1000, 0x2000, 0x8000};
  static const uint8_t counts[] = {0, 1, 3, 255};
  for (unsigned c = 0; c < sizeof cats / sizeof cats[0]; c++) {
    for (unsigned item = 0; item <= 0x14; item++) {
      for (unsigned k = 0; k < sizeof counts; k++) {
        ww(slot, 0, cats[c]);
        wb(slot, 2, (uint8_t)item);
        wb(slot, 3, counts[k]);
        DIFF(16, 0x20c8, FAR_CDECL, LO(slot), HI(slot));
      }
    }
  }
}

static void t_design_values(void) {
  world_alloc();
  uint32_t f = g_fl[0];
  for (unsigned it = 0; it < 500; it++) {
    int kind = (int)(it % 3);
    DS_W(0x14c) = (uint16_t)rn(NPL);
    for (unsigned p = 0; p < NPL; p++)
      for (unsigned i = 0; i < 16; i++) design_random(g_dtab[p] + 0x8d * i, kind);
    fleet_random(0, rn(NPL), 0);
    fleet_counts(f);
    uint32_t d = g_dtab[rw(f, 2)] + 0x8d * rn(16);
    if (kind == 2) {
      DIFF(16, 0x1d1c, FAR_CDECL, LO(d), HI(d));
      DIFF(16, 0x1ca2, FAR_CDECL, LO(f), HI(f));
    } else if (kind == 0) {
      DIFF(16, 0x193e, FAR_CDECL, LO(f), HI(f));
      DIFF(16, 0x1a34, FAR_CDECL, LO(f), HI(f));
    } else {
      static const uint16_t groups[] = {0, 1, 2, 3, 0xffff, 5};
      DIFF(16, 0x1aea, FAR_CDECL, LO(f), HI(f), groups[it % 6], 0xffff);
      DIFF(16, 0x1aea, FAR_CDECL, LO(f), HI(f), groups[(it + 1) % 6], (uint16_t)rn(16));
    }
  }
  /* one design with one slot of each item alone: the sums stay below the caps of 16:0x193e (0xfa0) */
  fleet_random(0, 1, 0);
  for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), 0);
  ww(f, 0x0c, 1);
  for (unsigned cat = 0; cat < 3; cat++) {
    for (unsigned item = 0; item < 16; item++) {
      for (unsigned i = 0; i < 16; i++) design_random(g_dtab[1] + 0x8d * i, 0);
      uint32_t d0 = g_dtab[1];
      wb(d0, 0x7a, 1);
      ww(d0, 0, 3);
      ww(d0, 0x3a, cat == 0 ? 0x80 : cat == 1 ? 0x100 : 0x10);
      wb(d0, 0x3c, (uint8_t)item);
      wb(d0, 0x3d, (uint8_t)(1 + item % 3));
      if (cat == 0) {
        DIFF(16, 0x193e, FAR_CDECL, LO(f), HI(f));
        DIFF(16, 0x1a34, FAR_CDECL, LO(f), HI(f));
      } else if (cat == 1 && item < 10) {
        for (unsigned g = 0; g < 4; g++) DIFF(16, 0x1aea, FAR_CDECL, LO(f), HI(f), g, 0xffff);
      } else if (cat == 2 && item < 16) {
        DIFF(16, 0x1d1c, FAR_CDECL, LO(d0), HI(d0));
        DIFF(16, 0x1ca2, FAR_CDECL, LO(f), HI(f));
      }
    }
  }
  /* large counts: the sums are 32-bit and wrap; the result of 16:0x1aea is capped */
  for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), 0x7fff);
  for (unsigned p = 0; p < NPL; p++)
    for (unsigned i = 0; i < 16; i++) design_random(g_dtab[p] + 0x8d * i, 1);
  for (unsigned g = 0; g < 4; g++) DIFF(16, 0x1aea, FAR_CDECL, LO(f), HI(f), g, 0xffff);
  for (unsigned p = 0; p < NPL; p++)
    for (unsigned i = 0; i < 16; i++) design_random(g_dtab[p] + 0x8d * i, 0);
  DIFF(16, 0x193e, FAR_CDECL, LO(f), HI(f));
  DIFF(16, 0x1a34, FAR_CDECL, LO(f), HI(f));
  for (unsigned p = 0; p < NPL; p++)
    for (unsigned i = 0; i < 16; i++) design_random(g_dtab[p] + 0x8d * i, 2);
  DIFF(16, 0x1ca2, FAR_CDECL, LO(f), HI(f));
}

/* ---- 16:0x244c: fold a design's component numbers into the per-player tables at DS:0x27d6 ---- */

static void t_244c(void) {
  world_alloc();
  uint32_t d = gblock(0x8d);
  static const uint16_t cats[] = {0x20, 0x10, 0x01, 0x04, 0x08, 0x40, 0x80, 0x800, 0x1000, 0x02, 0x100, 0x200};
  for (unsigned it = 0; it < 600; it++) {
    fill_random(d, 0x8d);
    wb(d, 0, (uint8_t)rn(32));  /* the hull number looked up in category 0x4000 (2:0x50f8 knows 0 to 31; for another it leaves a stale pointer) */
    wb(d, 0x7a, (uint8_t)(chance(1, 8) ? 16 : rn(8)));
    for (unsigned k = 0; k < 16; k++) {
      uint16_t cat = cats[rn(sizeof cats / sizeof cats[0])];
      static const uint8_t its[] = {7, 0x12, 8, 6, 9, 4, 0, 1, 2, 3, 5};
      ww(d, (uint16_t)(0x3a + 4 * k), cat);
      wb(d, (uint16_t)(0x3a + 4 * k + 2), chance(3, 5) ? its[rn(sizeof its)] : (uint8_t)rn(10));
      wb(d, (uint16_t)(0x3a + 4 * k + 3), chance(1, 6) ? 0 : chance(1, 4) ? (uint8_t)rnd() : (uint8_t)(1 + rn(9)));
    }
    if (it % 3 == 0) {
      /* the hull entries of the component table live in the code segment image (2:0x14ac + 0x8f * hull);
       * their bytes +2 to +7 are signed: make some of them negative (the same image serves both runs) */
      uint16_t e = (uint16_t)(0x14ac + 0x8f * rb_(d, 0) + 2);
      for (unsigned k = 0; k < 6; k++) *(uint8_t *)LIN(MK_FP(SEG_SEL(2), (uint16_t)(e + k))) = (uint8_t)(chance(1, 2) ? 0x80 + rn(0x80) : rn(0x80));
    }
    for (unsigned k = 0; k < 6; k++) DS_B(0x27d6 + k) = (uint8_t)(chance(1, 3) ? 0xf0 + rn(16) : rn(120));
    for (unsigned k = 0; k < 12; k++) DS_B(0x27dc + k) = (uint8_t)(chance(1, 3) ? 0x16 + rn(6) : chance(1, 5) ? (uint8_t)rnd() : rn(25));
    DS_W(0x14c) = (uint16_t)rn(4);
    uint16_t player = (uint16_t)(it % 7 == 0 ? 0xffff : rn(4));
    DIFF(16, 0x244c, FAR_CDECL, LO(d), HI(d), player);
  }
}

/* ---- 16:0x0e10: how much of a hull class a fleet needs; the entries it reads are poked ---- */

/* The category 0x200 entries of the component table 2:0x50f8 sit in the code segment image at
 * 2:(0x0f + 0x38 * item), item 0 to 15; the function reads the words at entry +0x34 and +0x36, which the
 * scenarios overwrite (the same image serves both runs). */
static void poke_entry_0200(unsigned item, uint16_t w34, uint16_t w36) {
  uint16_t e = (uint16_t)(0x0f + 0x38 * item);
  memcpy(LIN(MK_FP(SEG_SEL(2), (uint16_t)(e + 0x34))), &w34, 2);
  memcpy(LIN(MK_FP(SEG_SEL(2), (uint16_t)(e + 0x36))), &w36, 2);
}
static uint16_t pick_word(void) {
  static const uint16_t v[] = {0, 1, 2, 3, 5, 10, 100, 1000, 8000, 0x7fff, 0xffff, 0xfffe, 0x8000, 0xff00};
  return chance(1, 6) ? (uint16_t)rnd() : v[rn(sizeof v / sizeof v[0])];
}
static void t_0e10(void) {
  for (unsigned it = 0; it < 900; it++) {
    unsigned i1 = rn(16), i2 = rn(16);
    if (it % 9 == 0) i2 = i1;
    poke_entry_0200(i1, pick_word(), pick_word());
    poke_entry_0200(i2, pick_word(), pick_word());
    uint16_t n = chance(1, 3) ? (uint16_t)rn(40) : pick_word();
    uint16_t d = chance(1, 3) ? (uint16_t)rn(40) : pick_word();
    DS_W(0x14c) = (uint16_t)rn(4);
    DS_W(0x5c00) = 0x1234;
    DIFF(16, 0x0e10, FAR_CDECL, i1 | (it % 4 == 0 ? 0x4200 : 0), i2 | (it % 4 == 1 ? 0x8100 : 0), n, d, 0x5c00);
  }
  /* the cases around the 32-bit products of 2500 and 10000: d between a and 5a, so that the steps 2500 * (5a - d) / a
   * lie below 10000 and the final percentage depends on every digit */
  for (unsigned it = 0; it < 650; it++) {
    unsigned i1 = rn(16), i2 = rn(16);
    uint16_t v = (uint16_t)(1 + rn(40)), a = (uint16_t)(1 + rn(3000)), b = it % 3 == 0 ? (uint16_t)(1 + rn(3000)) : a;
    poke_entry_0200(i1, a, v);
    poke_entry_0200(i2, b, 0);
    uint16_t lo = a < b ? a : b;
    uint16_t d = (uint16_t)(lo + 1 + rn(4u * lo - 1));
    uint16_t n = (uint16_t)rn(5u * v + 1);
    DIFF(16, 0x0e10, FAR_CDECL, i1, i2, n, d, 0x5c00);
  }
}

/* ---- 16:0x0962: apply damage to a fleet's ships ---- */

/* The pieces it reads: the fleet (counts +0x0c, damage words +0x2c, waypoint list with the second waypoint
 * giving the planet by id or by position), the owner's design table (word +0x28: how much of the hull
 * class the design counters; word +0x38: its strength, never 0 here because the original divides by it), the
 * player record (attribute 14: the value 7 turns the damage per design to zero), the planet positions
 * DS:0x31ba (count DS:0x7a), the component table entries of category 0x200 (poked, as for 16:0x0e10),
 * the random generator 8:0x8730 (run for real in both versions: its seeds are DGROUP words). The
 * message buffer (6:0x7344 runs for real: msg_state sets its buffer and the players' message flags). The
 * double: 10:0x6c1c (the fleet update with the two 0x7c-byte records on the stack: both are hashed at the call). */
static void t_0962(void) {
  world_alloc();
  uint32_t f = g_fl[0];
  DS_W(0x007a) = 6;
  for (unsigned it = 0; it < 642; it++) {
    stubs_clear_all();
    msg_state();
    unsigned owner = rn(NPL);
    fleet_random(0, owner, 2 + rn(2));
    for (unsigned i = 0; i < 16; i++) {
      uint16_t c = chance(1, 4) ? 0 : (uint16_t)(chance(1, 6) ? 100 + rn(200) : 1 + rn(40));
      ww(f, (uint16_t)(0x0c + 2 * i), c);
      ww(f, (uint16_t)(0x2c + 2 * i), chance(1, 3) ? 0 : (uint16_t)rnd());
    }
    /* waypoint 1: by position (type != 1) or by planet id (type 1, id at +4) */
    uint16_t w1 = 4 + 0x12;
    if (chance(1, 3)) {
      wb(g_wp[0], (uint16_t)(w1 + 7), (uint8_t)(1 | (rnd() & 0xf0)));
      ww(g_wp[0], (uint16_t)(w1 + 4), (uint16_t)rn(900));
    } else {
      wb(g_wp[0], (uint16_t)(w1 + 7), (uint8_t)(2 + rn(5)));
      uint32_t xy = chance(1, 5) ? rnd() : 0x00010000u * (1 + rn(6)) + 7 * rn(6);
      wd(g_wp[0], w1, xy);
      for (unsigned k = 0; k < 6; k++) ds_w32((uint16_t)(0x31ba + 4 * k), 0x00010000u * (k + 1) + 7 * k);
    }
    if (chance(1, 12)) DS_W(0x007a) = (uint16_t)(chance(1, 2) ? 0 : 0x8001); else DS_W(0x007a) = 6;
    DS_B(0x5a64 + owner * 0xc0 + 0x3e + 14) = (uint8_t)(chance(1, 3) ? 7 : rn(16));
    /* the two items and the entries they select: v = entry 1 +0x36, w1 = entry 1 +0x34, w2 = entry 2 +0x34 */
    unsigned a = rn(16), b = it % 4 == 0 ? a : rn(16);
    uint16_t v = (uint16_t)(1 + rn(40)), ea = (uint16_t)(1 + rn(3000)), eb = it % 3 == 0 ? (uint16_t)(1 + rn(3000)) : ea;
    poke_entry_0200(a, ea, v);
    poke_entry_0200(b, eb, 0);
    uint16_t lo = ea < eb ? ea : eb;
    uint16_t c = (uint16_t)rn(5u * v + 1);
    if (chance(1, 25)) c = (uint16_t)(5u * v + 1 + rn(50));      /* 5v < n: 0xffff, the message 0xe3 */
    for (unsigned i = 0; i < 16; i++) {
      uint32_t dsg = g_dtab[owner] + 0x8d * i;
      fill_random(dsg, 0x8d);
      uint16_t d = (uint16_t)(lo + 1 + rn(4u * lo - 1));
      unsigned r = rn(40);
      if (r == 0) d = (uint16_t)(5u * lo + 1 + rn(30));          /* 5w < d: 0xfffe, the message 0xe4 */
      else if (r < 4) d = (uint16_t)(5u * lo - rn(lo));         /* the step 2500 * (5w - d) / w is near 0 */
      else if (r < 9) d = (uint16_t)rn(lo + 1);                  /* d <= w: the steps are skipped */
      if (it % 8 == 7 && i < 3) {
        /* more than 65535 ships lost in all (the message 0xeb): three designs of 0x4000 or more ships whose
         * percentage fails (d == 5w gives a step of 0, so the whole stack is lost without a random roll) */
        d = (uint16_t)(5u * lo);
        ww(f, (uint16_t)(0x0c + 2 * i), (uint16_t)(0x4000 + rn(0x3000)));
      }
      ww(dsg, 0x28, d);
      int16_t w38 = (int16_t)(chance(1, 10) ? -(int)(1 + rn(300)) : (int)(1 + rn(400)));
      if (chance(1, 20)) w38 = (int16_t)(1 + rn(3));
      ww(dsg, 0x38, (uint16_t)w38);
    }
    if (it >= 640) {
      /* the message kind boundaries: design 0 loses its whole stack (d == 5w: loss 100), designs 1 and 2 lose
       * nothing (d <= w and n <= v: loss 0; two of them, as the survivor count drops by one for every design
       * lost), the other designs are empty. it 640: 4 + 6 + 6 ships, lost 4 = total / 4 (so not 0xe8); it 641:
       * 4 + 2 + 2 ships, lost 4 = total / 2 (so not 0xea). Both give 0xe9. */
      uint16_t n1 = it == 640 ? 6 : 2;
      b = (a + 1) % 16;                                          /* two entries (a == b would zero v) */
      poke_entry_0200(a, ea, v);
      poke_entry_0200(b, ea, 0);
      lo = ea;
      for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), i == 0 ? 4 : i < 3 ? n1 : 0);
      ww(g_dtab[owner], 0x28, (uint16_t)(5u * lo));
      ww(g_dtab[owner] + 0x8d, 0x28, lo);
      ww(g_dtab[owner] + 0x8d * 2, 0x28, lo);
      c = v;
    }
    DS_W(0x151e) = (uint16_t)rnd();
    DS_W(0x1520) = (uint16_t)rnd();
    DS_W(0x1522) = (uint16_t)rnd();
    DS_W(0x1524) = (uint16_t)rnd();
    DIFF(16, 0x0962, FAR_CDECL, LO(f), HI(f), a, b, c);
  }
}

/* ---- 16:0x17c2: where a fleet that must fight goes: pick the first waypoint's task ---- */

/* The fleet world: NFL fleets, owners 0 to 2 in ascending order most of the time, positions from a small
 * set, designs with category 0x80 slots (the weapons 16:0x193e counts, via 2:0x50f8), flags with bit 2
 * (dead) set now and then; a planet record (its owner word at +2); the player attribute 14 (8 enables the
 * test). The waypoint list of the fleet under test is written by the function. */
static void t_17c2(void) {
  world_alloc();
  uint32_t planet = gblock(0x40);
  for (unsigned it = 0; it < 700; it++) {
    for (unsigned p = 0; p < NPL; p++)
      for (unsigned i = 0; i < 16; i++) design_random(g_dtab[p] + 0x8d * i, chance(1, 8) ? 2 : 0);
    unsigned k = rn(NFL);
    for (unsigned i = 0; i < NFL; i++) {
      unsigned owner = it % 5 == 4 ? rn(3) : (i * 3) / NFL;
      fleet_random(i, owner, 2);
      ww(g_fl[i], 8, (uint16_t)(1 + rn(2)));
      ww(g_fl[i], 0xa, (uint16_t)(1 + rn(2)));
      wb(g_fl[i], 5, (uint8_t)(chance(1, 4) ? 4 | rnd() : rnd() & ~4u));
      fleet_counts(g_fl[i]);
      if (chance(1, 5)) for (unsigned j = 0; j < 16; j++) ww(g_fl[i], (uint16_t)(0x0c + 2 * j), 0);
    }
    if (it % 3 == 0) {
      /* small weapon numbers (16:0x193e stops at 0xfa0): one ship of a design with a single weapon slot */
      for (unsigned p = 0; p < NPL; p++) {
        uint32_t d0 = g_dtab[p];
        wb(d0, 0x7a, 1);
        ww(d0, 0x3a, 0x80);
        wb(d0, 0x3c, (uint8_t)rn(7));
        wb(d0, 0x3d, (uint8_t)(1 + rn(2)));
      }
      for (unsigned i = 0; i < NFL; i++) {
        for (unsigned j = 0; j < 16; j++) ww(g_fl[i], (uint16_t)(0x0c + 2 * j), (uint16_t)(j == 0 ? 1 + rn(2) : 0));
        if (chance(2, 3)) { ww(g_fl[i], 8, 1); ww(g_fl[i], 0xa, 1); }
      }
    }
    if (it % 11 == 10) wd(g_ftab, (uint16_t)(4 * rn(NFL)), 0);
    else for (unsigned i = 0; i < NFL; i++) wd(g_ftab, (uint16_t)(4 * i), g_fl[i]);
    DS_W(0x553a) = chance(1, 10) ? (uint16_t)rn(3) : NFL;
    unsigned owner = rw(g_fl[k], 2);
    ww(planet, 2, chance(1, 2) ? 0xffff : (uint16_t)(chance(1, 3) ? owner : rn(3)));
    DS_B(0x5a64 + owner * 0xc0 + 0x3e + 14) = (uint8_t)(chance(4, 5) ? 8 : rn(16));
    fill_random(g_wp[k], 4 + 0x12 * 2);
    wb(g_fl[k], 0x60, (uint8_t)rnd());
    DIFF(16, 0x17c2, FAR_CDECL, LO(g_fl[k]), HI(g_fl[k]), LO(planet), HI(planet));
  }
  DS_W(0x553a) = NFL;
}

/* ---- 16:0x1e02: a fleet's rating, 0 to 98, from the cost of its ships (long arithmetic, then x87 when large) ---- */

/* Designs for the rating: word +0x28 (a size), slots of the categories whose values 16:0x20c8 knows,
 * small (the long path) or large counts (the sum above 0xfa0, which switches to the x87 sums). */
static void t_1e02(void) {
  world_alloc();
  uint32_t f = g_fl[0];
  static const uint16_t cats[] = {1, 2, 4, 8, 0x10, 0x80, 0x800, 0x1000, 0x20};
  static const uint8_t its[][4] = {{8, 0, 0, 0}, {6, 0, 0, 0}, {4, 6, 0, 0}, {7, 9, 0, 0}, {0x12, 0, 0, 0}, {6, 7, 0, 0}, {0, 1, 2, 3}, {4, 0, 0, 0}, {5, 0, 0, 0}};
  for (unsigned it = 0; it < 700; it++) {
    unsigned owner = rn(NPL);
    unsigned mode = it % 5;   /* 0, 1: small numbers; 2: large counts; 3: huge; 4: mixed */
    for (unsigned i = 0; i < 16; i++) {
      uint32_t dsg = g_dtab[owner] + 0x8d * i;
      fill_random(dsg, 0x8d);
      ww(dsg, 0x28, mode >= 2 && chance(1, 2) ? (uint16_t)rnd() : (uint16_t)(1 + rn(300)));
      unsigned n = rn(7);
      wb(dsg, 0x7a, (uint8_t)n);
      for (unsigned k = 0; k < 16; k++) {
        unsigned c = rn(sizeof cats / sizeof cats[0]);
        ww(dsg, (uint16_t)(0x3a + 4 * k), cats[c]);
        wb(dsg, (uint16_t)(0x3a + 4 * k + 2), its[c][rn(4)]);
        wb(dsg, (uint16_t)(0x3a + 4 * k + 3), (uint8_t)(mode >= 2 ? (chance(1, 2) ? rnd() : 1 + rn(20)) : 1 + rn(4)));
      }
    }
    fleet_random(0, owner, 0);
    for (unsigned i = 0; i < 16; i++) {
      uint16_t c = chance(1, 3) ? 0 : (uint16_t)(mode == 3 ? 0x7000 + rn(0x0fff) : mode == 2 ? 1 + rn(30000) : 1 + rn(40));
      if (chance(1, 30)) c = (uint16_t)(0x8000 + rn(10));  /* negative as a signed word: skipped */
      ww(f, (uint16_t)(0x0c + 2 * i), c);
    }
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), chance(1, 4) ? 0 : (uint32_t)(chance(1, 8) ? rnd() >> 4 : rn(2000)));
    DS_B(0x5a64 + owner * 0xc0 + 0x3e + 14) = (uint8_t)(chance(1, 3) ? 1 : rn(8));
    DIFF(16, 0x1e02, FAR_CDECL, LO(f), HI(f));
  }
  /* each reason to switch to the x87 sums alone: the long product sum * ca would wrap past 2^31 without it */
  for (unsigned scen = 0; scen < 3; scen++) {
    unsigned ndes = scen == 1 ? 2 : 1;
    fleet_random(0, 1, 0);
    for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), 0);
    for (unsigned i = 0; i < ndes; i++) {
      uint32_t dsg = g_dtab[1] + 0x8d * i;
      fill_random(dsg, 0x8d);
      wb(dsg, 0x7a, 1);
      ww(dsg, 0x3a, 0x80);                 /* item 6: 0x3c per count */
      wb(dsg, 0x3c, 6);
      /* scen 0: ca = 600000 > 500000, sum = 3960 <= 0xfa0; scen 1: two designs with ca = 480000, sum = 3960
       * (b = 1.9e9 after the first, above 100000000 at the second); scen 2: ca = 450000, sum = 4980 > 0xfa0 */
      wb(dsg, 0x3d, (uint8_t)(scen == 2 ? 83 : 66));
      ww(dsg, 0x28, scen == 0 ? 60000 : scen == 1 ? 60000 : 50000);
      ww(f, (uint16_t)(0x0c + 2 * i), scen == 0 ? 10 : scen == 1 ? 8 : 9);
    }
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), 0);
    DS_B(0x5a64 + 1 * 0xc0 + 0x3e + 14) = 0;
    DIFF(16, 0x1e02, FAR_CDECL, LO(f), HI(f));
  }
  /* the mapping of the ratio to 0 to 98: one design with a controlled ratio b / a (sum 100 .. 1500) */
  for (unsigned ratio = 0; ratio < 1600; ratio += (ratio < 40 || (ratio > 90 && ratio < 130) || (ratio > 290 && ratio < 330) ? 1 : 7)) {
    uint32_t dsg = g_dtab[1];
    fill_random(dsg, 0x8d);
    ww(dsg, 0x28, 10);
    wb(dsg, 0x7a, 1);
    ww(dsg, 0x3a, 0x80);       /* the value of the slot: 0x3c for item 6, times its count */
    wb(dsg, 0x3c, 6);
    wb(dsg, 0x3d, (uint8_t)(ratio / 60));
    fleet_random(0, 1, 0);
    for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), 0);
    ww(f, 0x0c, 7);
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), 0);
    DS_B(0x5a64 + 1 * 0xc0 + 0x3e + 14) = (uint8_t)(ratio % 2 ? 1 : 0);
    DIFF(16, 0x1e02, FAR_CDECL, LO(f), HI(f));
  }
}

/* ---- 16:0x1060: tidy the waypoints of every fleet ---- */

/* Planets for 7:0x1e4 (run for real): DS:0xb6 is a far pointer to four 0x38-byte records, DS:0x4d96 and
 * DS:0x7a their count, so the planet with id n is record n. */
static uint32_t g_planets;
static void planets_alloc(void) {
  g_planets = gblock(4 * 0x38);
  ds_w32(0x00b6, g_planets);
  DS_W(0x4d96) = 4;
  DS_W(0x007a) = 4;
}
static void planets_random(void) {
  fill_random(g_planets, 4 * 0x38);
  for (unsigned i = 0; i < 4; i++) {
    ww(g_planets, (uint16_t)(0x38 * i), (uint16_t)i);
    ww(g_planets, (uint16_t)(0x38 * i + 2), (uint16_t)rn(3));
    ww(g_planets, (uint16_t)(0x38 * i + 0x30), chance(1, 3) ? (uint16_t)(rnd() & 0xfc00) : chance(1, 2) ? (uint16_t)((rnd() & 0xfc00) | (1u << rn(10))) : (uint16_t)rnd());
  }
}
/* Waypoints of the fleet: wp[k] at list +4 + 0x12 * k; +0 the position, +4 the target id, +6 the
 * word of task nibbles, +7 the high byte whose low nibble is the type (2 = follow a fleet). */
static void waypoints_random(unsigned i, unsigned n) {
  fleet_random(i, rn(3), n);
  for (unsigned k = 0; k < 12; k++) {
    uint16_t w = (uint16_t)(4 + 0x12 * k);
    wb(g_wp[i], (uint16_t)(w + 7), (uint8_t)((chance(1, 2) ? 2 : rn(16)) | (chance(1, 3) ? 0x20 : 0) | (rnd() & 0xd0)));
    wb(g_wp[i], (uint16_t)(w + 6), (uint8_t)(chance(1, 3) ? 1 + 3 * rn(2) : rnd()));
  }
}
static void t_1060(void) {
  world_alloc();
  planets_alloc();
  for (unsigned it = 0; it < 800; it++) {
    stubs_clear_all();
    msg_state();
    planets_random();
    for (unsigned i = 0; i < NFL; i++) {
      waypoints_random(i, chance(1, 8) ? rn(2) : 2 + rn(3));
      wb(g_fl[i], 5, (uint8_t)(chance(1, 6) ? 0x80 | (rnd() & 0x7f) : (rnd() & 0x7f)));
      ww(g_fl[i], 6, chance(1, 3) ? 0xffff : (uint16_t)rn(4));  /* a planet that exists: 7:0x1e4 answers null for another id */
      /* the fleet's own position and waypoint 1's: equal half of the time (the waypoints then slide down) */
      ww(g_fl[i], 8, (uint16_t)(1 + rn(2)));
      ww(g_fl[i], 0xa, (uint16_t)(1 + rn(2)));
      if (chance(1, 2)) wd(g_wp[i], 0x16, rd_fl_xy(g_fl[i]));
    }
    if (it % 9 == 8) wd(g_ftab, (uint16_t)(4 * rn(NFL)), 0);
    else for (unsigned i = 0; i < NFL; i++) wd(g_ftab, (uint16_t)(4 * i), g_fl[i]);
    DS_W(0x553a) = it % 13 == 12 ? (uint16_t)rn(2) : NFL;
    /* 7:0x14a8 answers a fleet (or none) for every following waypoint it is asked about; 10:0x6230 leaves 1 waypoint
     * (count 1) or no change */
    for (unsigned q = 0; q < 12; q++) {
      uint32_t t = chance(1, 3) ? 0 : g_fl[rn(NFL)];
      stub_push(S_7_14A8, LO(t), HI(t));
      stub_push(S_10_6230, chance(2, 3) ? 1 : 0xffff, 0);
    }
    DIFF(16, 0x1060, FAR_CDECL);
  }
  /* one fleet whose waypoint 1 slides into slot 0 with the task 8, 10:0x6230 leaving one waypoint: the planet decides */
  static const uint16_t w30[] = {0, 1, 0x100, 0x200, 0x3ff, 0x400, 0x600, 0x201, 0xfc00};
  DS_W(0x553a) = 1;
  for (unsigned om = 0; om < 2; om++) {
    for (unsigned w = 0; w < sizeof w30 / sizeof w30[0]; w++) {
      for (unsigned pl = 0; pl < 5; pl++) {
        stubs_clear_all();
        msg_state();
        planets_random();
        waypoints_random(0, 2);
        ww(g_fl[0], 2, 1);
        wb(g_fl[0], 5, 0);
        ww(g_fl[0], 6, pl == 4 ? 0xffff : (uint16_t)pl);
        ww(g_fl[0], 8, 5);
        ww(g_fl[0], 0xa, 6);
        wd(g_wp[0], 0x16, rd_fl_xy(g_fl[0]));
        wb(g_wp[0], 0x16 + 6, 8);          /* the task nibble 8 of the waypoint that moves into slot 0 */
        wb(g_wp[0], 0x16 + 7, 0x01);       /* its type: not "follow a fleet" */
        for (unsigned i = 0; i < 4; i++) {
          ww(g_planets, (uint16_t)(0x38 * i + 2), (uint16_t)(om ? 1 : 2));
          ww(g_planets, (uint16_t)(0x38 * i + 0x30), w30[w]);
        }
        wd(g_ftab, 0, g_fl[0]);
        stub_push(S_10_6230, 1, 0);
        DIFF(16, 0x1060, FAR_CDECL);
      }
    }
  }
  DS_W(0x553a) = NFL;
}

/* ---- 16:0x13f8: send a fleet to a planet: waypoint 1, the warp speed ---- */

/* The pieces it reads: the planet record P (owner +2, flags +5 bit 1, byte +0x2c the starbase design
 * index, word +0x30 the id of the target planet + 1 in its low 10 bits), the planet table for 7:0x1e4,
 * the planet positions DS:0x31ba, the starbase design tables DS:0x10c (a design with a category 0x200 slot
 * of item below 7, which 7:0xaf8 answers), the hull table entries of 2:0x507c (word +0x34, poked),
 * the doubles 10:0x678c (the speed to start with: its default is set per scenario) and 10:0x6312 (the
 * cost of a speed: its queued values are the answers in the order of the calls). 7:0x28ea (a distance as a
 * double at DS:0x18fc), 7:0x1e4 and 7:0xaf8, 2:0x507c and 16:0x0e10 run for real. */
static uint32_t g_sbd[NPL];
static void sbd_alloc(void) {
  for (unsigned p = 0; p < NPL; p++) {
    g_sbd[p] = gblock(10 * 0x8d);
    ds_w32((uint16_t)(0x10c + 4 * p), g_sbd[p]);
  }
}
static void t_13f8(void) {
  world_alloc();
  planets_alloc();
  sbd_alloc();
  uint32_t planet = g_planets;   /* P is one of the four records */
  for (unsigned it = 0; it < 800; it++) {
    stubs_clear_all();
    planets_random();
    for (unsigned i = 0; i < 4; i++) {
      ww(g_planets, (uint16_t)(0x38 * i + 2), chance(2, 3) ? 1 : (uint16_t)rn(3));
      wb(g_planets, (uint16_t)(0x38 * i + 5), (uint8_t)(chance(4, 5) ? 2 | (rnd() & 0xfd) : rnd() & 0xfd));
      wb(g_planets, (uint16_t)(0x38 * i + 0x2c), (uint8_t)((rnd() & 0xf0) | rn(10)));
      ww(g_planets, (uint16_t)(0x38 * i + 0x30), (uint16_t)((1 + rn(4)) | (rnd() & 0xfc00)));
    }
    for (unsigned k = 0; k < 4; k++) ds_w32((uint16_t)(0x31ba + 4 * k), (uint32_t)(rn(800) | (rn(800) << 16)));
    for (unsigned p = 0; p < NPL; p++) {
      fill_random(g_sbd[p], 10 * 0x8d);
      for (unsigned d = 0; d < 10; d++) {
        uint32_t dsg = g_sbd[p] + 0x8d * d;
        ww(dsg, 0, (uint16_t)rn(0x40));                       /* the hull word 2:0x507c looks up */
        wb(dsg, 0x7a, 4);
        for (unsigned k = 0; k < 4; k++) {
          ww(dsg, (uint16_t)(0x3a + 4 * k), chance(1, 2) ? 0x200 : (uint16_t)rnd());
          wb(dsg, (uint16_t)(0x3a + 4 * k + 2), (uint8_t)(chance(3, 4) ? rn(7) : rn(12)));
          wb(dsg, (uint16_t)(0x3a + 4 * k + 3), (uint8_t)(chance(3, 4) ? 1 + rn(3) : 0));
        }
      }
    }
    for (unsigned h = 0; h < 32; h++) {      /* the hull entries' word +0x34: zero or not */
      uint16_t w = chance(1, 5) ? 0 : (uint16_t)(1 + rn(500));
      memcpy(LIN(MK_FP(SEG_SEL(2), (uint16_t)(0x14ac + 0x8f * h + 0x34))), &w, 2);
    }
    for (unsigned h = 0; h < 16; h++) poke_entry_0200(h, (uint16_t)(1 + rn(3000)), (uint16_t)(1 + rn(40)));
    unsigned pi_sel = rn(4);
    uint32_t f = g_fl[0];
    fleet_random(0, 1, 4);
    {
      /* near the target planet half of the time: a short distance, so that 16:0x0e10 has a small n to answer */
      unsigned tid = (rw(planet, (uint16_t)(0x38 * pi_sel + 0x30)) & 0x3ff);
      uint32_t txy = tid >= 1 && tid <= 4 ? rd_ds32((uint16_t)(0x31b6 + 4 * tid)) : 0;
      ww(f, 8, chance(1, 2) ? (uint16_t)(LO(txy) + rn(3)) : (uint16_t)rn(800));
      ww(f, 0xa, chance(1, 2) ? (uint16_t)(HI(txy) + rn(3)) : (uint16_t)rn(800));
    }
    for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), chance(2, 3) ? 0 : (uint16_t)(1 + rn(30)));
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), chance(3, 4) ? 0 : rn(500));
    wd(f, 0x5c, chance(1, 3) ? 0 : (uint32_t)(rnd() >> (chance(1, 2) ? 8 : 0)));
    DS_W(0x14c) = (uint16_t)rn(NPL);
    for (unsigned i = 0; i < 16; i++) {
      uint32_t dsg = g_dtab[1] + 0x8d * i;
      fill_random(dsg, 0x8d);
      ww(dsg, 0x28, (uint16_t)(chance(1, 3) ? rn(60) : rn(4000)));
    }
    stub_def(S_10_678C, (uint16_t)rn(14), 0);
    if (it % 4 == 3) {
      /* edges: the target is within a few units (the distance, hence "need", is computed here the way the x87 does it);
       * every cost the double answers equals the need, and the fleet's funds are the same number; and all entries of
       * category 0x200 have the same word +0x34 (E), the designs' word +0x28 being E - 1, E, E + 1, 5E - 1, 5E, 5E + 1 */
      unsigned tid = rw(planet, (uint16_t)(0x38 * pi_sel + 0x30)) & 0x3ff;
      uint32_t txy = tid >= 1 && tid <= 4 ? rd_ds32((uint16_t)(0x31b6 + 4 * tid)) : 0;
      unsigned ddx = rn(3), ddy = rn(3);
      ww(f, 8, (uint16_t)(LO(txy) + ddx));
      ww(f, 0xa, (uint16_t)(HI(txy) + ddy));
      uint16_t need_h = (uint16_t)(int)(__builtin_sqrt((double)(ddx * ddx + ddy * ddy)) + 0.999);
      wd(f, 0x5c, need_h);
      uint16_t e = (uint16_t)(2 + rn(chance(1, 2) ? 8 : 3000));  /* a small E makes the step 2500 * (4E - 1) / E visible in the percentage */
      for (unsigned h = 0; h < 16; h++) poke_entry_0200(h, e, (uint16_t)(1 + rn(40)));
      static const int deltas[] = {-1, 0, 1};
      for (unsigned i = 0; i < 16; i++) {
        uint16_t w = (uint16_t)(chance(1, 2) ? e + deltas[rn(3)] : 5u * e + deltas[rn(3)]);
        ww(g_dtab[1] + 0x8d * i, 0x28, w);
      }
      for (unsigned q = 0; q < 12; q++) stub_push(S_10_6312, need_h, 0);
    } else
    for (unsigned q = 0; q < 12; q++) stub_push(S_10_6312, chance(1, 2) ? (uint16_t)rn(1500) : (uint16_t)rnd(), (uint16_t)(chance(3, 4) ? 0 : rn(3)));
    unsigned pi = pi_sel;
    DS_W(0x14c) = 1;
    DIFF(16, 0x13f8, FAR_CDECL, LO(f), HI(f), (uint16_t)(LO(planet) + 0x38 * pi), HI(planet));
  }
  /* the 16:0x0e10 branch for sure: same owner, flags, no cargo, a starbase item below 7 on both planets, one design
   * whose word +0x28 sweeps the edges d = E - 1 .. E + 1 and 5E - 1 .. 5E + 1 of the entries' word +0x34 (E small) */
  for (unsigned it = 0; it < 90; it++) {
    stubs_clear_all();
    planets_random();
    for (unsigned i = 0; i < 4; i++) {
      ww(g_planets, (uint16_t)(0x38 * i + 2), 1);
      wb(g_planets, (uint16_t)(0x38 * i + 5), 2);
      wb(g_planets, (uint16_t)(0x38 * i + 0x2c), (uint8_t)rn(10));
      ww(g_planets, (uint16_t)(0x38 * i + 0x30), (uint16_t)(1 + rn(4)));
    }
    for (unsigned k = 0; k < 4; k++) ds_w32((uint16_t)(0x31ba + 4 * k), (uint32_t)(rn(20) | (rn(20) << 16)));
    for (unsigned d = 0; d < 10; d++) {
      uint32_t dsg = g_sbd[1] + 0x8d * d;
      fill_random(dsg, 0x8d);
      ww(dsg, 0, (uint16_t)rn(0x20));
      wb(dsg, 0x7a, 1);
      ww(dsg, 0x3a, 0x200);
      wb(dsg, 0x3c, (uint8_t)rn(7));
      wb(dsg, 0x3d, 1);
    }
    uint16_t e = (uint16_t)(2 + rn(9));
    for (unsigned h = 0; h < 16; h++) poke_entry_0200(h, e, (uint16_t)(1 + rn(40)));
    static const int deltas[] = {-1, 0, 1};
    uint32_t f = g_fl[0];
    fleet_random(0, 1, 4);
    for (unsigned i = 0; i < 16; i++) ww(f, (uint16_t)(0x0c + 2 * i), (uint16_t)(i == 0 ? 3 : 0));
    for (unsigned k = 0; k < 4; k++) wd(f, (uint16_t)(0x4c + 4 * k), 0);
    wd(f, 0x5c, 1000);
    ww(g_dtab[1], 0x28, (uint16_t)(it % 2 ? e + deltas[rn(3)] : 5u * e + deltas[rn(3)]));
    ww(f, 8, 1);
    ww(f, 0xa, 1);
    DS_W(0x14c) = 1;
    stub_def(S_10_678C, (uint16_t)rn(14), 0);
    for (unsigned q = 0; q < 12; q++) stub_push(S_10_6312, (uint16_t)rn(40), 0);
    DIFF(16, 0x13f8, FAR_CDECL, LO(f), HI(f), (uint16_t)(LO(planet) + 0x38 * rn(4)), HI(planet));
  }
}

/* ---- 16:0x21bc MERGEFLEETSDLG: the dialog that picks the fleets to merge ---- */

/* The list box 0x51 lists DS:0x27f2 fleets, their indices in the word array DS:0x53a0 points to; the double
 * 7:0x1a70 (a fleet id -> its name: a near pointer to a string, queued per call) stands in for the name
 * lookup, which needs the whole fleet and planet world; 31:0x61b0 and 31:0xab72 (a message box and a
 * confirmation; the second answers the queued value) are doubles with no argument. */
static void t_21bc(void) {
  world_alloc();
  zip_state();
  uint16_t dlg = mkwin(WS_OVERLAPPED | WS_VISIBLE, 10, 10, 300, 300, 0, 0);
  uint16_t lb = mkctl("LISTBOX", WS_CHILD | WS_VISIBLE | 0x0008 /* LBS_MULTIPLESEL */, 5, 5, 200, 150, dlg, 0x51);
  uint16_t other = mkwin(WS_CHILD | WS_VISIBLE, 5, 200, 50, 20, dlg, 0x52);
  const uint16_t dcs = GetStockObject(0);
  DS_W(0x53a0) = 0x5d00;
  for (unsigned i = 0; i < NFL; i++) {
    fleet_random(i, 0, rn(4));
    ww(g_fl[i], 0, (uint16_t)(i + 1));
    wd(g_ftab, (uint16_t)(4 * i), g_fl[i]);
    memset(&DS_B(0x5c40 + 0x20 * i), 0, 0x20);
    snprintf((char *)&DS_B(0x5c40 + 0x20 * i), 0x20, "Fleet &%u", i + 1);
  }
  DS_W(0x553a) = NFL;
  /* WM_ERASEBKGND and WM_CTLCOLOR */
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x14, dcs, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, 0, 0x14, 0, 0x1234, 0x5678);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x19, dcs, 6, lb);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x19, dcs, 6, other);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x19, dcs, 6, 0);
  /* WM_INITDIALOG: n fleets, in an order of the table, some with more than one waypoint; the current fleet DS:0x4b8c */
  for (unsigned it = 0; it < 60; it++) {
    stubs_clear_all();
    SendMessage(lb, 0x405, 0, 0);                       /* LB_RESETCONTENT */
    unsigned n = it % 7 < 6 ? it % 7 : 1 + rn(4);
    DS_W(0x27f2) = (uint16_t)n;
    for (unsigned k = 0; k < n; k++) {
      DS_W(0x5d00 + 2 * k) = (uint16_t)rn(NFL);
      stub_push(S_7_1A70, (uint16_t)(0x5c40 + 0x20 * rn(NFL)), 0);
    }
    for (unsigned i = 0; i < NFL; i++) ww(g_fl[i], 0x62, (uint16_t)rn(4));
    DS_W(0x4b8c) = (uint16_t)(1 + rn(NFL));
    DS_B(0x791) = (uint8_t)(it % 3 == 0 ? 8 : 0);
    DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x110, 0, 0, 0);
  }
  DIFF(16, 0x21bc, FAR_PASCAL, 0, 0x110, 0, 0, 0);       /* no dialog: no list box */
  DS_W(0x27f2) = 0;
  DS_B(0x791) = 8;
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x110, 0, 0, 0);
  DS_B(0x791) = 0;
  DS_W(0x27f2) = 0x8000;                                  /* negative: no fleet */
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x110, 0, 0, 0);
  /* WM_COMMAND: OK (1) and Cancel (2) with a selection set in the list box, select all (0x7f8) / none (0x7f9), help (0x76) */
  for (unsigned it = 0; it < 40; it++) {
    stubs_clear_all();
    SendMessage(lb, 0x405, 0, 0);
    unsigned n = 1 + it % 5;
    DS_W(0x27f2) = (uint16_t)n;
    for (unsigned k = 0; k < n; k++) {
      DS_W(0x5d00 + 2 * k) = (uint16_t)k;
      SendMessage(lb, 0x401, 0, fstr("item"));
      SendMessage(lb, 0x406, chance(1, 2) ? 1 : 0, k);   /* LB_SETSEL */
    }
    DS_B(0x791) = (uint8_t)(it % 4 < 2 ? 8 : 0);
    stub_push(S_31_AB72, (uint16_t)(it % 2), 0);
    DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 1, 0, 0);
    DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 2, 0, 0);
    DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x7f8, 0, 0);
    DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x7f9, 0, 0);
  }
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x76, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x77, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0, 0, 0);
  DS_W(0x27f2) = 0;
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 1, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x7f8, 0, 0);
  DS_W(0x27f2) = 0x8000;
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 2, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x111, 0x7f9, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x0, 0, 0, 0);
  DIFF(16, 0x21bc, FAR_PASCAL, dlg, 0x112, 0, 0, 0);
  DestroyWindow(dlg);
}

int main(void) {
  g_only = getenv("SEG16_ONLY");
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
  stubs_init();
  zip_state();
  if (want("0606")) t_0606();
  if (want("0000")) t_0000();
  if (want("names")) t_name_dialogs();
  if (want("mask")) t_designs_mask();
  if (want("133e")) t_133e();
  if (want("slotval")) t_slot_value();
  if (want("designval")) t_design_values();
  if (want("244c")) t_244c();
  if (want("0e10")) t_0e10();
  if (want("0962")) t_0962();
  if (want("17c2")) t_17c2();
  if (want("1060")) t_1060();
  if (want("13f8")) t_13f8();
  if (want("21bc")) t_21bc();
  if (want("1e02")) t_1e02();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
