/* Self-tests for the differential-test driver (harness/difftest.c), task stars-dwf.3.
 * They use synthetic original code in scratch code selectors, so no inventory
 * function is involved; hand-written expected values are allowed here only. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness/difftest.h"
#include "test.h"

#define EXE "original/stars26i.exe"

/* int16 add2(int16 a, int16 b), FAR PASCAL: PUSH BP; MOV BP,SP; MOV AX,[BP+8];
 * ADD AX,[BP+6]; POP BP; RETF 4 */
static const uint8_t ADD2[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x08, 0x03, 0x46, 0x06, 0x5d, 0xca, 0x04, 0x00};

static int16_t g_bias; /* mutation: added to the C result */

static uint32_t ad_add2(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  int16_t a = (int16_t)DT_ARG16(ss, sp, 6), b = (int16_t)DT_ARG16(ss, sp, 4);
  return (uint16_t)(int16_t)(a + b + g_bias);
}

static uint16_t g_add2_sel;

static void reg_add2(void) {
  DtEntry e = {g_add2_sel, 0, "add2", FAR_PASCAL, ad_add2, 1};
  difftest_register(&e);
}

static void test_missing_then_pass(void) {
  uint16_t args[] = {3, 4};
  DtResult r;
  g_add2_sel = harness_alloc_code(ADD2, sizeof ADD2);
  CHECK(g_add2_sel != 0);
  difftest_set_extent(g_add2_sel, 0, sizeof ADD2, "add2");

  /* Red: no C function registered. */
  difftest_unregister_all();
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_MISSING);
  CHECK_EQ(r.ret & 0xFFFF, 7);
  CHECK(strstr(r.msg, "MISSING") != NULL);

  /* Green. */
  reg_add2();
  g_bias = 0;
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(r.ret & 0xFFFF, 7);

  /* Wrap-around: 0x7FFF + 1 = 0x8000. */
  args[0] = 0x7FFF;
  args[1] = 1;
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(r.ret & 0xFFFF, 0x8000);

  r = difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 2, 1000, 1);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "%s\n", r.msg);
}

static void test_mutation(void) {
  uint16_t args[] = {3, 4};
  DtResult r;
  reg_add2();
  g_bias = 1; /* the C version returns a + b + 1 */
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "AX") != NULL);
  fprintf(stdout, "mutation report: %s\n", r.msg);
  r = difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 2, 1000, 1);
  CHECK_EQ(r.status, DT_FAIL);
  fprintf(stdout, "mutation fuzz report: %s\n", r.msg);
  g_bias = 0;
}

/* Original: MOV word [0x0100],0x1234 (DS is DGROUP); RETF */
static const uint8_t WRITER[] = {0xc7, 0x06, 0x00, 0x01, 0x34, 0x12, 0xcb};

static uint32_t ad_writer(uint16_t ss, uint16_t sp, Regs16 *r) {
  uint16_t v = 0x1235;
  (void)ss; (void)sp; (void)r;
  memcpy(LIN(MK_FP(MEM_SEL_DGROUP, 0x0100)), &v, 2);
  return 0;
}

static void test_memory(void) {
  uint16_t sel = harness_alloc_code(WRITER, sizeof WRITER);
  DtEntry e;
  DtResult r;
  memset(&e, 0, sizeof e);
  e.seg = sel; e.name = "writer"; e.conv = FAR_PASCAL; e.adapter = ad_writer;
  difftest_register(&e);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "selector 0x0118, offset 0x0100") != NULL);
  fprintf(stdout, "memory report: %s\n", r.msg);
  /* Same write in both versions passes. */
  {
    static const uint8_t same[] = {0xc7, 0x06, 0x00, 0x01, 0x35, 0x12, 0xcb};
    uint16_t sel2 = harness_alloc_code(same, sizeof same);
    e.seg = sel2;
    difftest_register(&e);
    r = difftest_run(sel2, 0, FAR_PASCAL, NULL, 0, NULL);
    CHECK_EQ(r.status, DT_PASS);
  }
}

static void api_handler(HCall *c) { (void)c; }
static uint32_t g_thunk;

static uint32_t ad_api_twice(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk), FP_OFF(g_thunk), FAR_PASCAL, 1, 5);
  CALL_ORIG(FP_SEG(g_thunk), FP_OFF(g_thunk), FAR_PASCAL, 1, 5);
  return 0;
}

static uint32_t ad_api_once(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk), FP_OFF(g_thunk), FAR_PASCAL, 1, 5);
  return 0;
}

static void test_api_log(void) {
  uint8_t code[] = {0x6a, 0x05, 0x9a, 0, 0, 0, 0, 0xcb}; /* PUSH 5; CALL FAR thunk; RETF */
  uint16_t sel;
  DtEntry e;
  DtResult r;
  g_thunk = harness_add_import("TESTMOD", 1, "TESTAPI", HK_PASCAL, 2, HR_NONE, api_handler);
  CHECK(g_thunk != 0);
  code[3] = (uint8_t)FP_OFF(g_thunk); code[4] = (uint8_t)(FP_OFF(g_thunk) >> 8);
  code[5] = (uint8_t)FP_SEG(g_thunk); code[6] = (uint8_t)(FP_SEG(g_thunk) >> 8);
  sel = harness_alloc_code(code, sizeof code);
  memset(&e, 0, sizeof e);
  e.seg = sel; e.name = "apicaller"; e.conv = FAR_PASCAL;

  e.adapter = ad_api_twice;
  difftest_register(&e);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "API log differs at index 1") != NULL);
  fprintf(stdout, "api log report: %s\n", r.msg);

  e.adapter = ad_api_once;
  difftest_register(&e);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "%s\n", r.msg);
}

static void test_coverage(void) {
  uint16_t args[] = {3, 4};
  char buf[256] = "";
  char path[96];
  FILE *fp;
  g_bias = 0;
  reg_add2();
  CHECK_EQ(difftest_diff(g_add2_sel, 0, FAR_PASCAL, args, 2), DT_PASS);
  CHECK(difftest_coverage(g_add2_sel, 0) == 100.0);
  snprintf(path, sizeof path, "build/coverage/%u_0000.txt", g_add2_sel);
  fp = fopen(path, "r");
  CHECK(fp != NULL);
  if (fp) {
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    buf[n] = 0;
    fclose(fp);
  }
  CHECK(strstr(buf, "instructions=6 executed=6") != NULL);
  CHECK(strstr(buf, "coverage=100.0%") != NULL);
}

static void test_isolation(void) {
  uint16_t args[] = {7, 8};
  size_t n = mem_size();
  uint8_t *before = malloc(n);
  DtResult r;
  memcpy(before, mem_arena(), n);
  g_bias = 1;
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  g_bias = 0;
  reg_add2();
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, args, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK(memcmp(before, mem_arena(), n) == 0);
  CHECK_EQ(harness_log_count(), 0);
  free(before);
}

static void test_rt_call_far_modes(void) {
  uint16_t args[] = {3, 4};
  g_bias = 1;
  reg_add2();
  rt_set_mode(RT_MODE_ORIGINAL);
  CHECK_EQ(rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, args, 2) & 0xFFFF, 7);
  rt_set_mode(RT_MODE_DECOMP);
  CHECK_EQ(rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, args, 2) & 0xFFFF, 8);
  rt_set_mode(RT_MODE_ORIGINAL);
  g_bias = 0;
}

static void test_frame(void) {
  uint16_t sv_sp, sv_bp;
  rt_ss = MEM_SEL_DGROUP;
  rt_sp = harness_stack_top();
  rt_bp = 0x1111;
  sv_sp = rt_sp; sv_bp = rt_bp;
  FRAME_ENTER(6);
  CHECK_EQ(rt_bp, sv_sp - 2);
  CHECK_EQ(rt_sp, sv_sp - 8);
  BP_LOCAL(uint16_t, -2) = 0xBEEF;
  CHECK_EQ(*(uint16_t *)LIN(MK_FP(MEM_SEL_DGROUP, rt_bp - 2)), 0xBEEF);
  FRAME_LEAVE();
  CHECK_EQ(rt_sp, sv_sp);
  CHECK_EQ(rt_bp, sv_bp);
  rt_sp = rt_bp = 0;
}

/* Original locals can sit at odd BP offsets (rule d): BP_LOCAL must be unaligned-safe (UBSan). */
static void test_frame_odd_offsets(void) {
  rt_ss = MEM_SEL_DGROUP;
  rt_sp = harness_stack_top();
  rt_bp = 0;
  FRAME_ENTER(8);
  BP_LOCAL(uint16_t, -3) = 0xBEEF;
  BP_LOCAL(uint32_t, -7) = 0x12345678u;
  CHECK_EQ(BP_LOCAL(uint16_t, -3), 0xBEEF);
  CHECK_EQ(BP_LOCAL(uint32_t, -7), 0x12345678u);
  CHECK_EQ(*(uint8_t *)LIN(MK_FP(MEM_SEL_DGROUP, (uint16_t)(rt_bp - 3))), 0xEF);
  FRAME_LEAVE();
  rt_sp = rt_bp = 0;
}


/* ======================================================================
 * Review fixes (stars-dwf.3 items A-K). Every test below was written first
 * and failed on the code before the fix (see the task report).
 * ====================================================================== */

static void regfn(uint16_t sel, const char *name, int conv, DtAdapter ad, int rw) {
  DtEntry e;
  memset(&e, 0, sizeof e);
  e.seg = sel; e.name = name; e.conv = conv; e.adapter = ad; e.ret_words = rw;
  difftest_register(&e);
}

static uint32_t ad_noop(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  return 0;
}

/* Runs fn in a forked child; true when it died of SIGABRT and its stderr
 * contains needle. */
typedef void (*VoidFn)(void);
static bool expect_abort(VoidFn fn, const char *needle) {
  int pfd[2];
  char buf[4096];
  size_t n = 0;
  ssize_t k;
  int st = 0;
  pid_t pid;
  if (pipe(pfd) != 0) return false;
  fflush(NULL);
  pid = fork();
  if (pid == 0) {
    struct rlimit rl = {0, 0};
    setrlimit(RLIMIT_CORE, &rl);
    close(pfd[0]);
    dup2(pfd[1], 2);
    close(pfd[1]);
    fn();
    _exit(0); /* did not abort */
  }
  close(pfd[1]);
  while (n < sizeof buf - 1 && (k = read(pfd[0], buf + n, sizeof buf - 1 - n)) > 0) n += (size_t)k;
  buf[n] = 0;
  close(pfd[0]);
  waitpid(pid, &st, 0);
  if (!(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT)) {
    fprintf(stderr, "expect_abort: child did not abort (status 0x%x); stderr: %s\n", st, buf);
    return false;
  }
  if (!strstr(buf, needle)) {
    fprintf(stderr, "expect_abort: stderr lacks \"%s\": %s\n", needle, buf);
    return false;
  }
  return true;
}

/* Builds "PUSH arg; CALL FAR thunk; RETF" in a scratch code segment. */
static uint16_t make_api_caller(uint32_t thunk, uint8_t arg) {
  uint8_t code[] = {0x6a, arg, 0x9a, 0, 0, 0, 0, 0xcb};
  code[3] = (uint8_t)FP_OFF(thunk); code[4] = (uint8_t)(FP_OFF(thunk) >> 8);
  code[5] = (uint8_t)FP_SEG(thunk); code[6] = (uint8_t)(FP_SEG(thunk) >> 8);
  return harness_alloc_code(code, sizeof code);
}

/* ---- A: argument slots written by the original ---- */

/* PUSH BP; MOV BP,SP; INC WORD [BP+6]; MOV AX,[BP+6]; POP BP; RETF 2 */
static const uint8_t INCARG[] = {0x55, 0x8b, 0xec, 0xff, 0x46, 0x06, 0x8b, 0x46, 0x06, 0x5d, 0xca, 0x02, 0x00};

static uint32_t ad_incarg(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  return (uint16_t)(DT_ARG16(ss, sp, 4) + 1);
}

static void test_A_arg_slot_writes(void) {
  uint16_t sel = harness_alloc_code(INCARG, sizeof INCARG);
  uint16_t a[] = {41};
  uint16_t top = harness_stack_top();
  DtResult r;
  regfn(sel, "incarg", FAR_PASCAL, ad_incarg, 1);
  r = difftest_run(sel, 0, FAR_PASCAL, a, 1, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(r.ret & 0xFFFF, 42);
  if (r.status != DT_PASS) fprintf(stderr, "A: %s\n", r.msg);

  /* The last byte of the stack (offset top-1) is the callee's frame: passes. */
  {
    /* MOV BYTE [off],0x55; RETF 2 (one argument word, so top-1 is the arg's high byte) */
    uint8_t in_stack[] = {0xc6, 0x06, (uint8_t)(top - 1), (uint8_t)((top - 1) >> 8), 0x55, 0xca, 0x02, 0x00};
    uint8_t in_heap[] = {0xc6, 0x06, (uint8_t)top, (uint8_t)(top >> 8), 0x55, 0xca, 0x02, 0x00};
    uint16_t s1 = harness_alloc_code(in_stack, sizeof in_stack);
    uint16_t s2 = harness_alloc_code(in_heap, sizeof in_heap);
    regfn(s1, "wr_top_m1", FAR_PASCAL, ad_noop, 0);
    regfn(s2, "wr_top", FAR_PASCAL, ad_noop, 0);
    r = difftest_run(s1, 0, FAR_PASCAL, a, 1, NULL);
    CHECK_EQ(r.status, DT_PASS);
    if (r.status != DT_PASS) fprintf(stderr, "A top-1: %s\n", r.msg);
    /* Offset top is the local heap, not the stack: a write there must FAIL. */
    r = difftest_run(s2, 0, FAR_PASCAL, a, 1, NULL);
    CHECK_EQ(r.status, DT_FAIL);
    CHECK(strstr(r.msg, "memory differs") != NULL);
  }
}

/* ---- B: the ORIGINAL run is forced to ORIGINAL mode ---- */

static void api_call_add2(HCall *c) {
  uint16_t a[] = {3, 4}, v;
  (void)c;
  v = (uint16_t)rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, a, 2);
  memcpy(LIN(MK_FP(MEM_SEL_DGROUP, 0x0100)), &v, 2);
}

static uint32_t g_thunk_b;
static uint32_t ad_call_thunk_b(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk_b), FP_OFF(g_thunk_b), FAR_PASCAL, 1, 5);
  return 0;
}

static void test_B_original_run_mode(void) {
  uint16_t sel;
  DtResult r;
  uint16_t args[] = {3, 4};
  g_thunk_b = harness_add_import("TESTMOD", 2, "CALLADD2", HK_PASCAL, 2, HR_NONE, api_call_add2);
  CHECK(g_thunk_b != 0);
  sel = make_api_caller(g_thunk_b, 5);
  regfn(sel, "b_outer", FAR_PASCAL, ad_call_thunk_b, 0);
  reg_add2();
  rt_set_mode(RT_MODE_DECOMP);

  g_bias = 1; /* the C add2 is wrong; only the emulated add2 is right */
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "memory differs") != NULL);
  /* the caller's mode is restored: still DECOMP, so the C add2 (7+1) is used */
  CHECK_EQ(rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, args, 2) & 0xFFFF, 8);

  g_bias = 0;
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_PASS);
  rt_set_mode(RT_MODE_ORIGINAL);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, args, 2) & 0xFFFF, 7);
}

/* ---- C: rt_call_far fallback at depth 0 keeps the C frame ---- */

static void test_C_fallback_keeps_frame(void) {
  uint16_t args[] = {3, 4}, top = harness_stack_top();
  uint32_t ret;
  difftest_unregister_all(); /* add2 is not decompiled */
  rt_set_mode(RT_MODE_DECOMP);
  rt_ss = MEM_SEL_DGROUP;
  rt_sp = top;
  rt_bp = 0;
  FRAME_ENTER(4);
  BP_LOCAL(uint16_t, -2) = 0x1234;
  BP_LOCAL(uint16_t, -4) = 0x5678;
  ret = rt_call_far(MK_FP(g_add2_sel, 0), FAR_PASCAL, args, 2);
  CHECK_EQ(ret & 0xFFFF, 7);
  CHECK_EQ(BP_LOCAL(uint16_t, -2), 0x1234);
  CHECK_EQ(BP_LOCAL(uint16_t, -4), 0x5678);
  FRAME_LEAVE();
  CHECK_EQ(rt_bp, 0);
  CHECK_EQ(rt_sp, top);
  rt_sp = rt_bp = 0;
  rt_set_mode(RT_MODE_ORIGINAL);
}

/* ---- E: C frame entered from a callback, then CALL_ORIG ---- */

static uint16_t g_e_l2, g_e_l4, g_e_bp_after, g_e_bp_inside;
static uint32_t g_e_ret;

static uint32_t ad_e_callback(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  FRAME_ENTER(4);
  BP_LOCAL(uint16_t, -2) = 0x1111;
  BP_LOCAL(uint16_t, -4) = 0x2222;
  g_e_bp_inside = rt_bp;
  g_e_ret = CALL_ORIG(g_add2_sel, 0, FAR_PASCAL, 2, 3, 4);
  g_e_l2 = BP_LOCAL(uint16_t, -2);
  g_e_l4 = BP_LOCAL(uint16_t, -4);
  FRAME_LEAVE();
  g_e_bp_after = rt_bp;
  return 0;
}

static uint16_t g_e_cb_sel;
static void api_e_callback(HCall *c) {
  (void)c;
  rt_call_far(MK_FP(g_e_cb_sel, 0), FAR_PASCAL, NULL, 0);
}

static void test_E_nested_c_frame(void) {
  uint32_t thunk = harness_add_import("TESTMOD", 3, "CALLBACK", HK_PASCAL, 2, HR_NONE, api_e_callback);
  uint16_t outer = make_api_caller(thunk, 5);
  static const uint8_t any[] = {0xcb};
  g_e_cb_sel = harness_alloc_code(any, sizeof any);
  regfn(g_e_cb_sel, "e_cb", FAR_PASCAL, ad_e_callback, 0);
  difftest_unregister_all();
  regfn(g_e_cb_sel, "e_cb", FAR_PASCAL, ad_e_callback, 0);
  rt_set_mode(RT_MODE_DECOMP);
  g_e_l2 = g_e_l4 = 0;
  g_e_bp_after = 0xFFFF;
  harness_call(outer, 0, HCONV_PASCAL, NULL, 0, NULL);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(g_e_ret & 0xFFFF, 7);
  CHECK_EQ(g_e_l2, 0x1111);
  CHECK_EQ(g_e_l4, 0x2222);
  CHECK_EQ(g_e_bp_after, 0);
  rt_set_mode(RT_MODE_ORIGINAL);
  rt_sp = rt_bp = 0;
}

/* stars-dwf.3 fix 1: a decompiled callee reached through rt_call_far inside a
 * callback must not overwrite the active C frame (rt_sp != 0) at depth > 0. */
static uint16_t g_f1_l2, g_f1_l4, g_f1_slot0, g_f1_slot0_after, g_f1_bp_in;
static uint32_t g_f1_ret;
static uint16_t g_f1_add2_sel, g_f1_cb_sel;

static uint32_t ad_f1_add2(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  uint16_t a = DT_ARG16(ss, sp, 6), b = DT_ARG16(ss, sp, 4);
  FRAME_ENTER(2);
  BP_LOCAL(uint16_t, -2) = 0xDEAD;
  FRAME_LEAVE();
  return (uint16_t)(a + b);
}

static uint32_t ad_f1_cb(uint16_t ss, uint16_t sp, Regs16 *r) {
  uint16_t args[2] = {3, 4};
  (void)ss; (void)sp; (void)r;
  FRAME_ENTER(4);
  BP_LOCAL(uint16_t, -2) = 0x1111;
  BP_LOCAL(uint16_t, -4) = 0x2222;
  g_f1_bp_in = rt_bp;
  g_f1_slot0 = BP_LOCAL(uint16_t, 0);
  g_f1_ret = rt_call_far(MK_FP(g_f1_add2_sel, 0), FAR_PASCAL, args, 2);
  g_f1_l2 = BP_LOCAL(uint16_t, -2);
  g_f1_l4 = BP_LOCAL(uint16_t, -4);
  g_f1_slot0_after = BP_LOCAL(uint16_t, 0);
  FRAME_LEAVE();
  return 0;
}

static void api_f1_callback(HCall *c) {
  (void)c;
  rt_call_far(MK_FP(g_f1_cb_sel, 0), FAR_PASCAL, NULL, 0);
}

static void test_F1_decomp_in_callback_keeps_frame(void) {
  static const uint8_t any[] = {0xcb};
  uint32_t thunk = harness_add_import("TESTMOD", 31, "F1CALLBACK", HK_PASCAL, 2, HR_NONE, api_f1_callback);
  uint16_t outer = make_api_caller(thunk, 5);
  g_f1_cb_sel = harness_alloc_code(any, sizeof any);
  g_f1_add2_sel = harness_alloc_code(any, sizeof any);
  difftest_unregister_all();
  regfn(g_f1_cb_sel, "f1_cb", FAR_PASCAL, ad_f1_cb, 0);
  regfn(g_f1_add2_sel, "f1_add2", FAR_PASCAL, ad_f1_add2, 1);
  rt_set_mode(RT_MODE_DECOMP);
  g_f1_l2 = g_f1_l4 = g_f1_slot0_after = 0xFFFF;
  harness_call(outer, 0, HCONV_PASCAL, NULL, 0, NULL);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(g_f1_ret & 0xFFFF, 7);
  CHECK_EQ(g_f1_l2, 0x1111);
  CHECK_EQ(g_f1_l4, 0x2222);
  CHECK_EQ(g_f1_slot0_after, g_f1_slot0);
  rt_set_mode(RT_MODE_ORIGINAL);
  rt_sp = rt_bp = 0;
}

/* stars-dwf.3 fix 2: the registry convention is checked against the DIFF's. */
static const uint8_t SUBC[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x06, 0x2b, 0x46, 0x08, 0x5d, 0xcb}; /* a-b, CDECL far */
static uint32_t ad_subc(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  return (uint16_t)(DT_ARG16(ss, sp, 4) - DT_ARG16(ss, sp, 6));
}
static uint16_t g_f2_sel;
static void child_conv_mismatch(void) {
  rt_set_mode(RT_MODE_DECOMP);
  rt_call_far(MK_FP(g_f2_sel, 0), FAR_PASCAL, (uint16_t[]){5, 5}, 2);
}

static void test_F2_convention_mismatch(void) {
  uint16_t a[] = {5, 5};
  DtResult r;
  g_f2_sel = harness_alloc_code(SUBC, sizeof SUBC);
  difftest_unregister_all();
  regfn(g_f2_sel, "subc", FAR_CDECL, ad_subc, 1);
  r = difftest_run(g_f2_sel, 0, FAR_PASCAL, a, 2, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "convention mismatch") != NULL);
  r = difftest_run(g_f2_sel, 0, FAR_CDECL, a, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK(expect_abort(child_conv_mismatch, "convention mismatch"));
}

/* stars-dwf.3 fix 4: DIFF restores a pending harness_stop_after_api. */
static void test_F4_stop_after_restored(void) {
  uint16_t a[] = {5, 5};
  DtResult r;
  difftest_unregister_all();
  regfn(g_f2_sel, "subc", FAR_CDECL, ad_subc, 1);
  harness_stop_after_api(3);
  r = difftest_run(g_f2_sel, 0, FAR_CDECL, a, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(harness_stop_after_api_remaining(), 3);
  harness_stop_after_api(0);
  r = difftest_run(g_f2_sel, 0, FAR_CDECL, a, 2, NULL);
  CHECK_EQ(harness_stop_after_api_remaining(), 0);
}

/* ---- D: REGISTER convention ---- */

static const uint8_t MOV_AX_BX[] = {0x8b, 0xc3, 0xcb};         /* MOV AX,BX; RETF */
static const uint8_t STC_RET[] = {0xf9, 0xcb};                  /* STC; RETF */
static const uint8_t NOP_RET[] = {0x90, 0xcb};                  /* NOP; RETF */

static uint32_t ad_reg_movaxbx(uint16_t ss, uint16_t sp, Regs16 *r) { (void)ss; (void)sp; r->ax = r->bx; return 0; }
static uint32_t ad_reg_movaxbx_bad(uint16_t ss, uint16_t sp, Regs16 *r) { (void)ss; (void)sp; r->ax = (uint16_t)(r->bx + 1); return 0; }
static uint32_t ad_reg_cf0(uint16_t ss, uint16_t sp, Regs16 *r) { (void)ss; (void)sp; r->flags &= (uint16_t)~1u; return 0; }
static uint32_t ad_reg_cf1(uint16_t ss, uint16_t sp, Regs16 *r) { (void)ss; (void)sp; r->flags |= 1u; return 0; }

static void test_D_register(void) {
  uint16_t s_mov = harness_alloc_code(MOV_AX_BX, sizeof MOV_AX_BX);
  uint16_t s_stc = harness_alloc_code(STC_RET, sizeof STC_RET);
  uint16_t s_nop = harness_alloc_code(NOP_RET, sizeof NOP_RET);
  Regs16 in;
  DtResult r;
  memset(&in, 0, sizeof in);
  in.bx = 0x1234;

  /* (c) a correct twin passes: DS/ES start the same on both sides */
  regfn(s_mov, "movaxbx", REGISTER, ad_reg_movaxbx, 0);
  r = difftest_run(s_mov, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "D(c): %s\n", r.msg);
  CHECK_EQ(r.regs.ax, 0x1234);
  r = difftest_run(s_mov, 0, REGISTER, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_PASS);
  /* the registers of the twin are really compared (REGISTER mismatch) */
  regfn(s_mov, "movaxbx", REGISTER, ad_reg_movaxbx_bad, 0);
  r = difftest_run(s_mov, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "register AX") != NULL);

  /* (b) FLAGS are compared: original sets CF, the twin clears it */
  regfn(s_stc, "stc", REGISTER, ad_reg_cf0, 0);
  r = difftest_run(s_stc, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "FLAGS") != NULL);
  regfn(s_stc, "stc", REGISTER, ad_reg_cf1, 0);
  r = difftest_run(s_stc, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "D(b): %s\n", r.msg);
  CHECK(r.regs.flags & 1);

  /* entry flags come from in->flags, not from the previous run (CF leaked above) */
  regfn(s_nop, "nop", REGISTER, ad_noop, 0);
  in.flags = 0;
  r = difftest_run(s_nop, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "D(flags entry): %s\n", r.msg);
  in.flags = 0x0045; /* CF PF ZF? (bit 0, 2, 6) */
  r = difftest_run(s_nop, 0, REGISTER, NULL, 0, &in);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(r.regs.flags & 0x0CD5, 0x0045);
}

static void test_D_fuzz_register_rejected(void) {
  uint16_t s_mov = harness_alloc_code(MOV_AX_BX, sizeof MOV_AX_BX);
  DtResult r;
  regfn(s_mov, "movaxbx", REGISTER, ad_reg_movaxbx, 0);
  r = difftest_fuzz_run(s_mov, 0, REGISTER, 0, 10, 1);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "REGISTER") != NULL);
  CHECK(strstr(r.msg, "fuzz") != NULL);
}

/* ---- G: gaps that let mutants survive ---- */

/* int32 sext(int16 a): MOV AX,[BP+6]; CWD  (FAR PASCAL, 1 arg) */
static const uint8_t SEXT[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x06, 0x99, 0x5d, 0xca, 0x02, 0x00};
static uint32_t ad_sext_good(uint16_t ss, uint16_t sp, Regs16 *r) { (void)r; return (uint32_t)(int32_t)(int16_t)DT_ARG16(ss, sp, 4); }
static uint32_t ad_sext_zero(uint16_t ss, uint16_t sp, Regs16 *r) { (void)r; return (uint32_t)DT_ARG16(ss, sp, 4); }

static void test_G_dx_mismatch(void) {
  uint16_t sel = harness_alloc_code(SEXT, sizeof SEXT);
  uint16_t a[] = {0xFFFF};
  DtResult r;
  regfn(sel, "sext", FAR_PASCAL, ad_sext_zero, 2);
  r = difftest_run(sel, 0, FAR_PASCAL, a, 1, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "DX") != NULL);
  regfn(sel, "sext", FAR_PASCAL, ad_sext_good, 2);
  r = difftest_run(sel, 0, FAR_PASCAL, a, 1, NULL);
  CHECK_EQ(r.status, DT_PASS);
  /* int16_t -1 round trip through the fuzzer: boundary values hit 0x8000/0xFFFF */
  r = difftest_fuzz_run(sel, 0, FAR_PASCAL, 1, 50, 7);
  CHECK_EQ(r.status, DT_PASS);
  regfn(sel, "sext", FAR_PASCAL, ad_sext_zero, 2);
  r = difftest_fuzz_run(sel, 0, FAR_PASCAL, 1, 50, 7);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "DX") != NULL);
}

static uint8_t g_arg_to_send = 5;
static uint16_t g_api_ret = 3;
static uint32_t g_thunk_g;
static void api_ret_global(HCall *c) { c->ret = g_api_ret; }

static uint32_t ad_g_send6(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk_g), FP_OFF(g_thunk_g), FAR_PASCAL, 1, 6);
  return 0;
}
static uint32_t ad_g_send5(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk_g), FP_OFF(g_thunk_g), FAR_PASCAL, 1, 5);
  return 0;
}
static uint32_t ad_g_ret7(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  g_api_ret = 7;
  CALL_ORIG(FP_SEG(g_thunk_g), FP_OFF(g_thunk_g), FAR_PASCAL, 1, 5);
  return 0;
}

static void test_G_api_log_content(void) {
  uint16_t sel;
  DtResult r;
  g_thunk_g = harness_add_import("TESTMOD", 4, "RETAPI", HK_PASCAL, 2, HR_RET16, api_ret_global);
  sel = make_api_caller(g_thunk_g, g_arg_to_send);
  /* same number of calls, different argument bytes */
  regfn(sel, "g_args", FAR_PASCAL, ad_g_send6, 0);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "API log differs at index 0") != NULL);
  /* same call and arguments, different return value */
  g_api_ret = 3;
  regfn(sel, "g_args", FAR_PASCAL, ad_g_ret7, 0);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "API log differs at index 0") != NULL);
  CHECK(strstr(r.msg, "ret=0x3") != NULL && strstr(r.msg, "ret=0x7") != NULL);
  g_api_ret = 3;
  regfn(sel, "g_args", FAR_PASCAL, ad_g_send5, 0);
  r = difftest_run(sel, 0, FAR_PASCAL, NULL, 0, NULL);
  CHECK_EQ(r.status, DT_PASS);
}

/* PUSH BP; MOV BP,SP; MOV AX,[BP+6]; OR AX,AX; JZ +1; INC AX; POP BP; RETF 2 */
static const uint8_t BRANCH[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x06, 0x0b, 0xc0, 0x74, 0x01, 0x40, 0x5d, 0xca, 0x02, 0x00};
static uint32_t ad_branch(uint16_t ss, uint16_t sp, Regs16 *r) {
  uint16_t a = DT_ARG16(ss, sp, 4);
  (void)r;
  return a ? (uint16_t)(a + 1) : 0;
}

static void test_G_coverage_below_100(void) {
  uint16_t sel = harness_alloc_code(BRANCH, sizeof BRANCH);
  uint16_t zero[] = {0}, five[] = {5};
  char path[96], buf[256] = "";
  FILE *fp;
  difftest_set_extent(sel, 0, sizeof BRANCH, "branch");
  regfn(sel, "branch", FAR_PASCAL, ad_branch, 1);
  CHECK_EQ(difftest_run(sel, 0, FAR_PASCAL, zero, 1, NULL).status, DT_PASS);
  CHECK(difftest_coverage(sel, 0) > 87.4 && difftest_coverage(sel, 0) < 87.6); /* 7 of 8 */
  snprintf(path, sizeof path, "build/coverage/%u_0000.txt", sel);
  fp = fopen(path, "r");
  CHECK(fp != NULL);
  if (fp) {
    size_t n = fread(buf, 1, sizeof buf - 1, fp);
    buf[n] = 0;
    fclose(fp);
  }
  CHECK(strstr(buf, "instructions=8 executed=7") != NULL);
  CHECK(strstr(buf, "coverage=87.5%") != NULL);
  CHECK_EQ(difftest_run(sel, 0, FAR_PASCAL, five, 1, NULL).status, DT_PASS);
  CHECK(difftest_coverage(sel, 0) == 100.0);
}

static void test_G_summary_and_worst_status(void) {
  uint16_t a[] = {3, 4};
  uint16_t sel = g_add2_sel;
  difftest_reset_summary();
  CHECK_EQ(difftest_recorded_status(sel, 0), -1);
  CHECK_EQ(difftest_summary(), 0);

  /* a recorded MISSING makes the summary fail */
  difftest_unregister_all();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_MISSING);
  CHECK_EQ(difftest_summary(), 1);
  difftest_reset_summary();
  CHECK_EQ(difftest_summary(), 0);

  /* a recorded FAIL makes it fail too */
  g_bias = 1;
  reg_add2();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_FAIL);
  CHECK_EQ(difftest_summary(), 1);
  g_bias = 0;
  difftest_reset_summary();

  /* worst status wins: PASS, MISSING, PASS, FAIL leaves MISSING */
  reg_add2();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_PASS);
  CHECK_EQ(difftest_recorded_status(sel, 0), DT_PASS);
  difftest_unregister_all();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_MISSING);
  reg_add2();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_PASS);
  g_bias = 1;
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_FAIL);
  CHECK_EQ(difftest_recorded_status(sel, 0), DT_MISSING);
  g_bias = 0;
  /* and FAIL beats PASS */
  difftest_reset_summary();
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_PASS);
  g_bias = 1;
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_FAIL);
  g_bias = 0;
  CHECK_EQ(difftest_diff(sel, 0, FAR_PASCAL, a, 2), DT_PASS);
  CHECK_EQ(difftest_recorded_status(sel, 0), DT_FAIL);
  difftest_reset_summary();
  CHECK_EQ(difftest_summary(), 0);
}

/* ---- H: fuzz edge cases ---- */

static void child_fuzz_17_args(void) { difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 17, 1, 1); }
static void child_fuzz_neg_args(void) { difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, -1, 1, 1); }

/* PUSH BP; MOV BP,SP; MOV AX,[BP+6]; POP BP; RETF 2 */
static const uint8_t IDENT1[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x06, 0x5d, 0xca, 0x02, 0x00};
static uint16_t g_seen[64];
static unsigned g_nseen;
static uint32_t ad_rec1(uint16_t ss, uint16_t sp, Regs16 *r) {
  uint16_t a = DT_ARG16(ss, sp, 4);
  (void)r;
  if (g_nseen < 64) g_seen[g_nseen++] = a;
  return a;
}
static uint32_t ad_corner(uint16_t ss, uint16_t sp, Regs16 *r) {
  int16_t a = (int16_t)DT_ARG16(ss, sp, 6), b = (int16_t)DT_ARG16(ss, sp, 4);
  (void)r;
  return (uint16_t)(int16_t)(a + b + ((uint16_t)a == 0x8000 && (uint16_t)b == 0x8000 ? 1 : 0));
}

static void test_H_fuzz_edges(void) {
  DtResult r;
  uint16_t sel = harness_alloc_code(IDENT1, sizeof IDENT1);
  static const uint16_t want[5] = {0, 1, 0x7FFF, 0x8000, 0xFFFF};
  CHECK(expect_abort(child_fuzz_17_args, "nargs"));
  CHECK(expect_abort(child_fuzz_neg_args, "nargs"));
  reg_add2();
  r = difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 2, 0, 1);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "iterations") != NULL);

  regfn(sel, "ident1", FAR_PASCAL, ad_rec1, 1);
  g_nseen = 0;
  r = difftest_fuzz_run(sel, 0, FAR_PASCAL, 1, 20, 12345);
  CHECK_EQ(r.status, DT_PASS);
  CHECK_EQ(g_nseen, 20);
  for (unsigned i = 0; i < 5 && i < g_nseen; i++) CHECK_EQ(g_seen[i], want[i]);

  /* a bug that only shows at 0x8000+0x8000 is found within the boundary iterations */
  {
    DtEntry e = {g_add2_sel, 0, "add2", FAR_PASCAL, ad_corner, 1};
    difftest_register(&e);
    r = difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 2, 10, 99);
    CHECK_EQ(r.status, DT_FAIL);
    if (r.status != DT_FAIL) fprintf(stderr, "H corner not found\n");
    reg_add2();
  }
}

/* ---- I: isolation ---- */

static void api_noop_h(HCall *c) { (void)c; }
static uint32_t g_thunk_i;
static void api_grow_arena(HCall *c) {
  (void)c;
  GlobalAlloc(GPTR, 3u * 1024 * 1024);
}
static void api_run_diff(HCall *c) {
  uint16_t a[] = {1, 2};
  (void)c;
  difftest_run(g_add2_sel, 0, FAR_PASCAL, a, 2, NULL);
}
static uint16_t g_nested_diff_sel;
static void child_nested_diff(void) { harness_call(g_nested_diff_sel, 0, HCONV_PASCAL, NULL, 0, NULL); }

static uint32_t ad_i_twice(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)ss; (void)sp; (void)r;
  CALL_ORIG(FP_SEG(g_thunk_i), FP_OFF(g_thunk_i), FAR_PASCAL, 1, 5);
  CALL_ORIG(FP_SEG(g_thunk_i), FP_OFF(g_thunk_i), FAR_PASCAL, 1, 5);
  return 0;
}

static void test_I_isolation(void) {
  uint16_t a[] = {7, 8};
  DtResult r;
  uint16_t sel2;
  uint32_t thunk;

  /* the scenario's API log survives a DIFF */
  g_thunk_i = harness_add_import("TESTMOD", 5, "NOOP", HK_PASCAL, 2, HR_NONE, api_noop_h);
  {
    uint16_t caller = make_api_caller(g_thunk_i, 9);
    harness_log_clear();
    harness_call(caller, 0, HCONV_PASCAL, NULL, 0, NULL);
    CHECK_EQ(harness_log_count(), 1);
    reg_add2();
    r = difftest_run(g_add2_sel, 0, FAR_PASCAL, a, 2, NULL);
    CHECK_EQ(r.status, DT_PASS);
    CHECK_EQ(harness_log_count(), 1);
    if (harness_log_count() == 1) {
      CHECK_EQ(harness_log_get(0)->ordinal, 5);
      CHECK_EQ(harness_log_get(0)->args[0], 9);
    }
    r = difftest_fuzz_run(g_add2_sel, 0, FAR_PASCAL, 2, 5, 1);
    CHECK_EQ(r.status, DT_PASS);
    CHECK_EQ(harness_log_count(), 1);
    harness_log_clear();
  }

  /* a pending harness_stop_after_api must not truncate the reference run */
  {
    uint8_t code[] = {0x6a, 5, 0x9a, 0, 0, 0, 0, 0x6a, 5, 0x9a, 0, 0, 0, 0, 0xcb};
    memcpy(code + 3, &(uint16_t){FP_OFF(g_thunk_i)}, 2);
    memcpy(code + 5, &(uint16_t){FP_SEG(g_thunk_i)}, 2);
    memcpy(code + 10, &(uint16_t){FP_OFF(g_thunk_i)}, 2);
    memcpy(code + 12, &(uint16_t){FP_SEG(g_thunk_i)}, 2);
    sel2 = harness_alloc_code(code, sizeof code);
    regfn(sel2, "i_twice", FAR_PASCAL, ad_i_twice, 0);
    harness_stop_after_api(1);
    r = difftest_run(sel2, 0, FAR_PASCAL, NULL, 0, NULL);
    CHECK_EQ(r.status, DT_PASS);
    if (r.status != DT_PASS) fprintf(stderr, "I stop_after: %s\n", r.msg);
    CHECK(!harness_stopped_early());
    harness_stop_after_api(0);
  }

  /* the arena growing during the ORIGINAL run is reported as such */
  thunk = harness_add_import("TESTMOD", 6, "GROW", HK_PASCAL, 2, HR_NONE, api_grow_arena);
  {
    uint16_t caller = make_api_caller(thunk, 1);
    regfn(caller, "i_grow", FAR_PASCAL, ad_noop, 0);
    r = difftest_run(caller, 0, FAR_PASCAL, NULL, 0, NULL);
    CHECK_EQ(r.status, DT_FAIL);
    CHECK(strstr(r.msg, "arena grew") != NULL);
    if (!strstr(r.msg, "arena grew")) fprintf(stderr, "I grow: %s\n", r.msg);
  }

  /* DIFF inside a nested harness_call aborts with a clear message */
  thunk = harness_add_import("TESTMOD", 7, "DIFFINSIDE", HK_PASCAL, 2, HR_NONE, api_run_diff);
  g_nested_diff_sel = make_api_caller(thunk, 1);
  CHECK(expect_abort(child_nested_diff, "nested"));
}

/* ---- J: NEAR conventions in CALL_ORIG / rt_call_far ---- */

static void child_orig_near(void) { rt_call_orig(g_add2_sel, 0, NEAR_PASCAL, 0); }
static void child_far_near(void) { rt_call_far(MK_FP(g_add2_sel, 0), NEAR_CDECL, NULL, 0); }

static void test_J_near_aborts(void) {
  CHECK(expect_abort(child_orig_near, "NEAR"));
  CHECK(expect_abort(child_far_near, "NEAR"));
}

/* ---- K: NEAR functions ---- */

/* NEAR PASCAL add2: PUSH BP; MOV BP,SP; MOV AX,[BP+6]; ADD AX,[BP+4]; POP BP; RET 4 */
static const uint8_t NADD2P[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x06, 0x03, 0x46, 0x04, 0x5d, 0xc2, 0x04, 0x00};
/* NEAR CDECL add2:   PUSH BP; MOV BP,SP; MOV AX,[BP+4]; ADD AX,[BP+6]; POP BP; RET */
static const uint8_t NADD2C[] = {0x55, 0x8b, 0xec, 0x8b, 0x46, 0x04, 0x03, 0x46, 0x06, 0x5d, 0xc3};

static uint32_t ad_nadd2p(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  return (uint16_t)((int16_t)DT_ARG16(ss, sp, 4) + (int16_t)DT_ARG16(ss, sp, 2) + g_bias);
}
static uint32_t ad_nadd2c(uint16_t ss, uint16_t sp, Regs16 *r) {
  (void)r;
  return (uint16_t)((int16_t)DT_ARG16(ss, sp, 2) + (int16_t)DT_ARG16(ss, sp, 4) + g_bias);
}

static void test_K_near(void) {
  uint16_t sp_ = harness_alloc_code(NADD2P, sizeof NADD2P);
  uint16_t sc_ = harness_alloc_code(NADD2C, sizeof NADD2C);
  uint16_t a[] = {3, 4};
  DtResult r;
  difftest_set_extent(sp_, 0, sizeof NADD2P, "nadd2p");
  regfn(sp_, "nadd2p", NEAR_PASCAL, ad_nadd2p, 1);
  regfn(sc_, "nadd2c", NEAR_CDECL, ad_nadd2c, 1);
  g_bias = 0;
  r = difftest_run(sp_, 0, NEAR_PASCAL, a, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "K pascal: %s\n", r.msg);
  CHECK_EQ(r.ret & 0xFFFF, 7);
  r = difftest_run(sc_, 0, NEAR_CDECL, a, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
  if (r.status != DT_PASS) fprintf(stderr, "K cdecl: %s\n", r.msg);
  CHECK_EQ(r.ret & 0xFFFF, 7);
  CHECK(difftest_coverage(sp_, 0) == 100.0);
  r = difftest_fuzz_run(sp_, 0, NEAR_PASCAL, 2, 200, 3);
  CHECK_EQ(r.status, DT_PASS);
  r = difftest_fuzz_run(sc_, 0, NEAR_CDECL, 2, 200, 3);
  CHECK_EQ(r.status, DT_PASS);
  /* mutation: a wrong C twin fails */
  g_bias = 1;
  r = difftest_run(sp_, 0, NEAR_PASCAL, a, 2, NULL);
  CHECK_EQ(r.status, DT_FAIL);
  CHECK(strstr(r.msg, "AX") != NULL);
  g_bias = 0;
  /* harness still healthy afterwards (CS between runs) */
  r = difftest_run(g_add2_sel, 0, FAR_PASCAL, a, 2, NULL);
  CHECK_EQ(r.status, DT_PASS);
}

int main(void) {
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  test_missing_then_pass();
  test_mutation();
  test_memory();
  test_api_log();
  test_coverage();
  test_isolation();
  test_rt_call_far_modes();
  test_frame();
  test_frame_odd_offsets();
  test_A_arg_slot_writes();
  test_B_original_run_mode();
  test_C_fallback_keeps_frame();
  test_E_nested_c_frame();
  test_F1_decomp_in_callback_keeps_frame();
  test_F2_convention_mismatch();
  test_F4_stop_after_restored();
  test_D_register();
  test_D_fuzz_register_rejected();
  test_G_dx_mismatch();
  test_G_api_log_content();
  test_G_coverage_below_100();
  test_G_summary_and_worst_status();
  test_H_fuzz_edges();
  test_I_isolation();
  test_J_near_aborts();
  test_K_near();
  difftest_reset_summary();
  reg_add2();
  g_bias = 0;
  {
    uint16_t a[] = {1, 2};
    CHECK_EQ(difftest_diff(g_add2_sel, 0, FAR_PASCAL, a, 2), DT_PASS);
  }
  CHECK_EQ(difftest_summary(), 0);
  harness_shutdown();
  return TEST_DONE();
}
