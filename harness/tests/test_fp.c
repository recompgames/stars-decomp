/* FP semantics (task stars-dwf.4): the x87 control word the original runs under, and
 * proof that runtime/fp80.c (SoftFloat extF80) reproduces the original's x87 basic
 * operations bit for bit. Decision record: docs/CONVENTIONS.md "Floating point". */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "harness/harness.h"
#include "harness/toolhelp_win87em_thunks.h"
#include "runtime/fp80.h"
#include "runtime/mem.h"
#include "test.h"

#define EXE "original/stars26i.exe"

/* PINNED_CW is the all-masked FNINIT word (0x037F). The equivalence tests below use it
 * because they compare NORMAL results, which do not depend on the mask bits (and an
 * unmasked overflow on the Unicorn FPU would trap instead of giving an IEEE result).
 * The control word full runs use is FPU_CW_DEFAULT (0x1330, owner decision). */
#define PINNED_CW 0x037Fu
#define NO_CW 0xFFFFu /* run_startup: leave the word harness_load / the registration set */
#define N_PAIRS 100000
#define N_PAIRS_CW 10000

/* ---- 1. Control word when WinMain (3:0x0000) is entered ---- */

static void h_inittask(HCall *c) {
  c->regs->ax = 1; c->regs->cx = 0x2000; c->regs->di = 1; c->regs->si = 0;
  c->regs->bx = 0x80; c->regs->es = MEM_SEL_DGROUP; c->regs->dx = 1;
}
static void h_one(HCall *c) { c->ret = 1; }
static void h_version(HCall *c) { c->ret = 0x0A03; }
static uint16_t g_heap_next = 0x4000;
static void h_localalloc(HCall *c) { c->ret = g_heap_next; g_heap_next += 0x100; }

static unsigned g_winmain_hits;
static uint16_t g_cw_at_winmain;
static uint32_t g_lin_winmain, g_lin_fninit, g_lin_fldcw;
static unsigned g_hit_fninit, g_hit_fldcw;
static uint16_t g_cw_before_fninit, g_cw_before_fldcw;
static void on_startup_insn(uint32_t lin, void *ud) {
  (void)ud;
  if (lin == g_lin_winmain) {
    if (g_winmain_hits++ == 0) g_cw_at_winmain = harness_fpcw();
    harness_stop_emu();
  } else if (lin == g_lin_fninit) {
    if (g_hit_fninit++ == 0) g_cw_before_fninit = harness_fpcw();
  } else if (lin == g_lin_fldcw) {
    if (g_hit_fldcw++ == 0) g_cw_before_fldcw = harness_fpcw();
  }
}

/* Runs the C runtime start-up (34:0x001A) up to the first instruction of WinMain with the
 * x87 control word set to cw_in beforehand. The start-up path executes FNSTCW [bp-6],
 * FNINIT (34:0x265a), compares, FNCLEX and FLDCW [bp-6] (34:0x26b1): net-neutral. */
static void run_startup(uint16_t cw_in) {
  uint32_t b3 = desc_base(MEM_SEL_SEG(3)), b34 = desc_base(MEM_SEL_SEG(34));
  uint32_t lo = b3 < b34 ? b3 : b34, hi = (b3 > b34 ? b3 : b34) + 0x10000;
  g_lin_winmain = b3; g_lin_fninit = b34 + 0x265a; g_lin_fldcw = b34 + 0x26b1;
  g_winmain_hits = g_hit_fninit = g_hit_fldcw = 0;
  harness_set_quiet(true);
  harness_register("KERNEL", 91, h_inittask);   /* InitTask */
  harness_register("KERNEL", 3, h_version);     /* GetVersion */
  harness_register("KERNEL", 30, h_one);        /* WaitEvent */
  harness_register("USER", 5, h_one);           /* InitApp */
  harness_register("KERNEL", 5, h_localalloc);  /* LocalAlloc */
  harness_register("KERNEL", 23, h_one);        /* LockSegment */
  harness_register("KERNEL", 24, h_one);        /* UnlockSegment */
  harness_trace(lo, hi, on_startup_insn, NULL);
  if (cw_in != NO_CW) harness_set_fpcw(cw_in);
  harness_call(MEM_SEL_SEG(34), 0x001A, HCONV_CDECL, NULL, 0, NULL);
  harness_trace(0, 0, NULL, NULL);
  harness_set_quiet(false);
}

/* Owner decision 2026-10-15 (stars-ckz): full runs use 0x1330. harness_load puts it in the
 * FPU and the start-up path preserves it, with and without the WIN87EM registration. */
static void test_default_control_word(void) {
  CHECK_EQ(FPU_CW_DEFAULT, 0x1330);
  CHECK_EQ(harness_fpcw(), FPU_CW_DEFAULT);          /* right after harness_load */
  run_startup(NO_CW);                                /* no registration of WIN87EM */
  CHECK_EQ(g_winmain_hits, 1);
  CHECK_EQ(g_cw_at_winmain, FPU_CW_DEFAULT);
  CHECK_EQ(g_cw_at_winmain & 0x3F, 0x30);            /* IM, DM, ZM, OM unmasked; UM, PM masked */
  CHECK_EQ((g_cw_at_winmain >> 8) & 3, 3);           /* PC = 64-bit */
}

/* With WIN87EM registered, __fpMath BX = 0 programs the word itself (runtime/win87em.c uses
 * the same FPU_CW_DEFAULT): entering with the old all-masked 0x037F must still give 0x1330
 * at WinMain, so a stale constant in win87em.c alone fails here. The registration is
 * permanent for the rest of the run, so this test runs last. */
static void test_default_control_word_registered(void) {
  toolhelp_win87em_register();
  harness_set_fpcw(0x037F);
  run_startup(NO_CW);
  CHECK_EQ(g_winmain_hits, 1);
  CHECK_EQ(g_cw_at_winmain, FPU_CW_DEFAULT);
}

/* 0x037F is the all-masked model word, entered explicitly: the path preserves it. */
static void test_control_word_at_winmain(void) {
  run_startup(PINNED_CW);
  CHECK_EQ(g_winmain_hits, 1);
  CHECK_EQ(g_cw_at_winmain, PINNED_CW);
  CHECK_EQ((g_cw_at_winmain >> 8) & 3, 3);   /* PC: 64-bit significand */
  CHECK_EQ((g_cw_at_winmain >> 10) & 3, 0);  /* RC: round to nearest even */
  CHECK_EQ(g_cw_at_winmain & 0x3F, 0x3F);    /* all exceptions masked */
}

/* Sentinel: 0x1332 (PC = 11, IM/DM/ZM/OM unmasked, the likely real word) entering the
 * start-up path is still 0x1332 at WinMain. The path does run FNINIT (34:0x265a, the
 * word is saved by FNSTCW before and restored by FLDCW at 34:0x26b1), so it is
 * net-neutral. Passes on the current code; it proves preservation. */
static void test_control_word_sentinel(void) {
  run_startup(0x1332);
  CHECK_EQ(g_winmain_hits, 1);
  CHECK_EQ(g_hit_fninit, 1);                 /* FNINIT really is on the path... */
  CHECK_EQ(g_hit_fldcw, 1);                  /* ...and so is the restoring FLDCW */
  CHECK_EQ(g_cw_before_fldcw, 0x037F);       /* FNINIT reset the word in between */
  CHECK_EQ(g_cw_at_winmain, 0x1332);
  run_startup(PINNED_CW);                    /* leave the harness model in place */
  CHECK_EQ(g_cw_at_winmain, PINNED_CW);
}

/* ---- 2. Equivalence of Unicorn x87 and fp80 ---- */

static uint64_t g_rng = 0x5EEDF00DCAFE1234ull;
static uint64_t rnd(void) {
  g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27;
  return g_rng * 0x2545F4914F6CDD1Dull;
}

static int is_nan(uint64_t u) { return ((u >> 52) & 0x7FF) == 0x7FF && (u << 12) != 0; }
static uint64_t bits(double d) { uint64_t u; memcpy(&u, &d, 8); return u; }
static double dbl(uint64_t u) { double d; memcpy(&d, &u, 8); return d; }

/* A random double: mostly moderate exponents, some across the whole range, a few
 * zeros and infinities. NaNs are not generated. */
static uint64_t rnd_bits(void) {
  uint64_t r = rnd();
  uint64_t sign = (r & 1) << 63, mant = (r >> 8) & 0xFFFFFFFFFFFFFull;
  unsigned pick = (unsigned)(r >> 1) % 100, e;
  if (pick < 2) return sign;                                  /* +-0 */
  if (pick < 3) return sign | (0x7FFull << 52);               /* +-inf */
  if (pick < 12) e = 1 + (unsigned)(rnd() % 2046);            /* any finite exponent */
  else e = 1023 - 40 + (unsigned)(rnd() % 81);
  return sign | ((uint64_t)e << 52) | mant;
}

/* Second operand: half of the time within +-60 binary orders of the first, which makes
 * alignment shifts, cancellation and sticky bits common. */
static uint64_t rnd_near(uint64_t a) {
  uint64_t b = rnd_bits();
  if (rnd() & 1) {
    int ea = (int)((a >> 52) & 0x7FF), eb = ea - 60 + (int)(rnd() % 121);
    if (ea != 0 && ea != 0x7FF && eb >= 1 && eb <= 2046)
      b = (b & ~(0x7FFull << 52)) | ((uint64_t)eb << 52);
  }
  return b;
}

#define BATCH 1000u

static uint16_t g_code_arith[4], g_code_fistp16, g_code_fistp32, g_code_fistp64, g_code_fsqrt, g_code_fyl2x, g_code_f2xm1;
static uint16_t g_data; /* data selector: BATCH records of 24 bytes (a, b, result) */

static uint16_t mk(const uint8_t *c, size_t n) {
  uint16_t s = harness_alloc_code(c, n);
  CHECK(s != 0);
  return s;
}

/* Each sequence loops over BATCH records at ES:SI (CX = count), 24 bytes apiece:
 *   fld qword es:[si]; <op> qword es:[si+8]; fstp/fistp es:[si+16]; add si,24; loop */
static void setup_code(void) {
  static const uint8_t op[4] = {0x44, 0x64, 0x4C, 0x74}; /* DC /0 /4 /1 /6: FADD, FSUB, FMUL, FDIV, [si+disp8] */
  for (int i = 0; i < 4; i++) {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0x26, 0xDC, op[i], 0x08, 0x26, 0xDD, 0x5C, 0x10,
                   0x83, 0xC6, 0x18, 0xE2, 0xF0, 0xCB};
    g_code_arith[i] = mk(c, sizeof c);
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0x26, 0xDF, 0x5C, 0x10, 0x83, 0xC6, 0x18, 0xE2, 0xF4, 0xCB};
    g_code_fistp16 = mk(c, sizeof c); /* fistp word */
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0x26, 0xDB, 0x5C, 0x10, 0x83, 0xC6, 0x18, 0xE2, 0xF4, 0xCB};
    g_code_fistp32 = mk(c, sizeof c); /* fistp dword */
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0x26, 0xDF, 0x7C, 0x10, 0x83, 0xC6, 0x18, 0xE2, 0xF4, 0xCB};
    g_code_fistp64 = mk(c, sizeof c); /* fistp qword */
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0xD9, 0xFA, 0x26, 0xDD, 0x5C, 0x10, 0x83, 0xC6, 0x18, 0xE2, 0xF2, 0xCB};
    g_code_fsqrt = mk(c, sizeof c); /* fld qword; fsqrt; fstp qword */
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0x26, 0xDD, 0x44, 0x08, 0xD9, 0xF1, 0x26, 0xDD, 0x5C, 0x10,
                   0x83, 0xC6, 0x18, 0xE2, 0xEE, 0xCB};
    g_code_fyl2x = mk(c, sizeof c); /* fld y; fld x; fyl2x; fstp qword */
    CHECK_EQ(harness_hook_x87_transc(desc_base(g_code_fyl2x) + 7), 0); /* the oracle's FYL2X hook */
  }
  {
    uint8_t c[] = {0x26, 0xDD, 0x04, 0xD9, 0xF0, 0x26, 0xDD, 0x5C, 0x10, 0x83, 0xC6, 0x18, 0xE2, 0xF2, 0xCB};
    g_code_f2xm1 = mk(c, sizeof c); /* fld qword; f2xm1; fstp qword */
    CHECK_EQ(harness_hook_x87_transc(desc_base(g_code_f2xm1) + 3), 0); /* the oracle's F2XM1 hook */
  }
  g_data = GlobalAlloc(GPTR, BATCH * 24u);
  CHECK(g_data != 0);
}

/* Runs a synthetic sequence under control word cw over the BATCH records in rec[]. */
static void run_x87(uint16_t sel, uint16_t cw, uint64_t (*rec)[3]) {
  Regs16 io;
  memcpy(LIN((uint32_t)g_data << 16), rec, BATCH * 24u);
  memset(&io, 0, sizeof io);
  io.es = g_data; io.si = 0; io.cx = BATCH;
  harness_set_fpcw(cw);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(harness_last_error(), 0);
  memcpy(rec, LIN((uint32_t)g_data << 16), BATCH * 24u);
}

typedef struct { unsigned tested, mism_fp80, mism_double; uint64_t ex_a, ex_b, ex_x87, ex_dbl; } Stat;

static Fp80 fp80_op(int i, Fp80 a, Fp80 b) {
  return i == 0 ? fp80_add(a, b) : i == 1 ? fp80_sub(a, b) : i == 2 ? fp80_mul(a, b) : fp80_div(a, b);
}
static double dbl_op(int i, double a, double b) {
  return i == 0 ? a + b : i == 1 ? a - b : i == 2 ? a * b : a / b;
}

static Stat test_arith(int op, uint16_t cw, unsigned n) {
  Stat s;
  uint64_t rec[BATCH][3];
  memset(&s, 0, sizeof s);
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) {
      rec[i][0] = rnd_bits(); rec[i][1] = rnd_near(rec[i][0]); rec[i][2] = 0;
    }
    run_x87(g_code_arith[op], cw, rec);
    /* run_x87 overwrote nothing but the result column; operands are intact. */
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t a = rec[i][0], b = rec[i][1], x = rec[i][2];
      uint64_t f = bits(fp80_to_f64(fp80_op(op, fp80_from_f64(dbl(a)), fp80_from_f64(dbl(b)))));
      uint64_t d = bits(dbl_op(op, dbl(a), dbl(b)));
      s.tested++;
      if (x != f && s.mism_fp80++ == 0)
        fprintf(stderr, "fp80 mismatch op %d cw %04x: %016llx %016llx x87 %016llx fp80 %016llx\n", op, cw,
                (unsigned long long)a, (unsigned long long)b, (unsigned long long)x, (unsigned long long)f);
      if (x != d && !is_nan(x) && s.mism_double++ == 0) { s.ex_a = a; s.ex_b = b; s.ex_x87 = x; s.ex_dbl = d; }
    }
  }
  return s;
}

/* FSQRT (stars-bfj.35: the C runtime's sqrt, 34:0x0d68, ends in FSQRT; 33:0x01a0 needs it).
 * Operands: random doubles (negative ones give the real indefinite), perfect squares, values
 * just around squares and small integers like the game's squared distances. Returns the number of
 * mismatches against the Unicorn x87 under cw. */
static unsigned test_sqrt(uint16_t cw, unsigned n) {
  unsigned bad = 0;
  uint64_t rec[BATCH][3];
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t r = rnd();
      unsigned k = (unsigned)(r >> 60) & 7;
      double v;
      if (k == 0) v = (double)(rnd() % 4000000000ull);
      else if (k == 1) { double q = (double)(rnd() % 65536); v = q * q; }
      else if (k == 2) { double q = (double)(rnd() % 65536); v = q * q + (double)((int)(rnd() % 3) - 1); }
      else if (k == 3) v = (double)(int32_t)(uint32_t)rnd();
      else if (k == 4) v = (double)(rnd() % 1000);
      else { rec[i][0] = rnd_bits(); rec[i][1] = 0; rec[i][2] = 0; continue; }
      rec[i][0] = bits(v); rec[i][1] = 0; rec[i][2] = 0;
    }
    run_x87(g_code_fsqrt, cw, rec);
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t a = rec[i][0], x = rec[i][2];
      uint64_t f = bits(fp80_to_f64(fp80_sqrt(fp80_from_f64(dbl(a)))));
      if (x != f && bad++ == 0)
        fprintf(stderr, "fsqrt mismatch cw %04x: %016llx x87 %016llx fp80 %016llx\n", cw,
                (unsigned long long)a, (unsigned long long)x, (unsigned long long)f);
    }
  }
  return bad;
}


/* FYL2X and F2XM1 (stars-nf6: the C runtime's pow, 34:0x0d84, runs both; src/crt34.h uses
 * fp80_fyl2x and fp80_f2xm1). Owner decision 2026-10-20: the oracle uses the same helpers. The
 * harness hooks FYL2X and F2XM1 (harness_hook_x87_transc in harness/harness.c; harness_load hooks
 * the original's sites in segment 34, setup_code hooks the synthetic sequences here), because
 * Unicorn's own helpers (QEMU 5.0) call the host libm, whose log and exp2 are not correctly rounded
 * (an earlier measurement, not recorded in the repository, found 1 ulp differences on a small
 * fraction of operands; see stars-5yb). What these two tests check is therefore NOT the helpers against an x87:
 * they check that the hooked oracle and the C path agree bit for bit on every operand (so the hook
 * runs, reads ST(0) / ST(1), writes the result, pops for FYL2X and skips the instruction), and that
 * the oracle returns the reference vectors of test_fp_vectors on their operands (where the host
 * libm is wrong, so an unhooked Unicorn fails there). The values of the helpers themselves are
 * pinned only by the reference vectors (test_fp_vectors); a real x87 is not compared (follow-up
 * issue). FYL2X operands: x positive normal (ST0), y finite (ST1). F2XM1 operands: |x| <= 1.
 * Each returns the number of operands where the oracle and the C path differ. */
static unsigned g_tr_exact, g_tr_total;
static uint64_t rnd_fyl2x_x(void) {
  uint64_t r = rnd();
  unsigned k = (unsigned)(r >> 60) & 7;
  double v;
  switch (k) {
    case 0: v = (double)(1 + rnd() % 100000); break;                  /* small integers: strengths */
    case 1: v = (double)(1 + rnd() % 4096) / 64.0; break;             /* binary fractions */
    case 2: v = 1.0 + (double)((int64_t)(rnd() % 2001) - 1000) * 0x1p-52; break; /* around 1 */
    case 3: v = dbl((uint64_t)(1023 + (int)(rnd() % 200) - 100) << 52); break;          /* exact powers of 2 */
    case 4: v = (double)(1 + rnd() % 1000) * 0.001; break;            /* decimal fractions */
    default: { /* random positive normal double over a wide exponent range */
      uint64_t b = (rnd() & 0x000FFFFFFFFFFFFFull) | ((uint64_t)(1023 - 300 + rnd() % 600) << 52);
      v = dbl(b);
    }
  }
  return bits(v);
}
static uint64_t rnd_fyl2x_y(void) {
  uint64_t r = rnd();
  unsigned k = (unsigned)(r >> 61) & 3;
  double v;
  switch (k) {
    case 0: v = (double)((int)(rnd() % 41) - 20); break;              /* small integers incl. 0, 1, negative */
    case 1: v = (double)((int)(rnd() % 4001) - 2000) / 64.0; break;
    case 2: v = dbl((rnd() & 0x800FFFFFFFFFFFFFull) | ((uint64_t)(1023 - 20 + rnd() % 40) << 52)); break;
    default: v = (double)((int)(rnd() % 200001) - 100000) * 0.001; break;
  }
  return bits(v);
}

static unsigned test_fyl2x(uint16_t cw, unsigned n) {
  unsigned bad = 0;
  uint64_t rec[BATCH][3];
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) {
      unsigned k = (unsigned)(rnd() % 16);
      rec[i][0] = rnd_fyl2x_y(); rec[i][1] = rnd_fyl2x_x(); rec[i][2] = 0;
      if (k == 0) rec[i][1] = bits(1.0);   /* log2(1) = 0 */
      if (k == 1) rec[i][0] = bits(0.0);
      if (k == 2) rec[i][0] = bits(1.0);
      if (k == 3) rec[i][1] = bits(2.0);
    }
    run_x87(g_code_fyl2x, cw, rec);
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t y = rec[i][0], x = rec[i][1], u = rec[i][2];
      uint64_t f = bits(fp80_to_f64(fp80_fyl2x(fp80_from_f64(dbl(x)), fp80_from_f64(dbl(y)))));
      g_tr_total++;
      if (u == f) g_tr_exact++;
      else if (bad++ == 0)
        fprintf(stderr, "fyl2x mismatch cw %04x: y %016llx x %016llx x87 %016llx fp80 %016llx\n", cw,
                (unsigned long long)y, (unsigned long long)x, (unsigned long long)u, (unsigned long long)f);
    }
  }
  return bad;
}

static unsigned test_f2xm1(uint16_t cw, unsigned n) {
  unsigned bad = 0;
  uint64_t rec[BATCH][3];
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) {
      unsigned k = (unsigned)(rnd() % 8);
      double v;
      if (k == 0) v = (double)((int)(rnd() % 129) - 64) / 64.0;               /* grid in [-1, 1] */
      else if (k == 1) v = (double)((int)(rnd() % 2001) - 1000) / 2000.0;     /* decimal-ish */
      else if (k == 2) v = dbl((rnd() & 0x800FFFFFFFFFFFFFull) | ((uint64_t)(1023 - 80 + rnd() % 80) << 52)); /* tiny to 0.5 */
      else v = (double)((int64_t)(rnd() % 4000001) - 2000000) / 4000000.0;    /* dense [-0.5, 0.5] */
      if (k == 3 && (rnd() & 7) == 0) v = (rnd() & 1) ? 1.0 : -1.0;
      if (k == 4 && (rnd() & 7) == 0) v = 0.0;
      rec[i][0] = bits(v); rec[i][1] = 0; rec[i][2] = 0;
    }
    run_x87(g_code_f2xm1, cw, rec);
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t a = rec[i][0], u = rec[i][2];
      uint64_t f = bits(fp80_to_f64(fp80_f2xm1(fp80_from_f64(dbl(a)))));
      g_tr_total++;
      if (u == f) g_tr_exact++;
      else if (bad++ == 0)
        fprintf(stderr, "f2xm1 mismatch cw %04x: %016llx x87 %016llx fp80 %016llx\n", cw,
                (unsigned long long)a, (unsigned long long)u, (unsigned long long)f);
    }
  }
  return bad;
}

/* Reference vectors, independent of the Unicorn and of the host libm. Each one is an
 * operand where the macOS libm (exp2, log) is 1 ulp off the exact value, so a helper that calls the
 * host libm fails here while the fixed-point helpers pass. Expected values from 70-digit decimal
 * arithmetic (Python decimal): F2XM1 as RN(2^x) - 1, the correctly rounded 2^x minus one (the
 * subtraction is exact), which for some vectors differs from 2^x - 1 rounded once to a double (that
 * gap is the subject of stars-5yb); FYL2X as log(x) / log(2.0) * y with the correctly rounded log
 * and double division and multiplication. They are the checks that pin the
 * values of fp80_fyl2x and fp80_f2xm1. They are also run through the hooked oracle, which must give
 * the same values (an oracle still on the host libm fails there). */
static void test_fp_vectors(int fyl2x) {
  static const uint64_t f2[][2] = {
    {0xbfebd938151a4378ull, 0xbfdcfd32dd2d3c26ull}, {0x3fe5a8cef672b884ull, 0x3fe3282994ed3dd6ull},
    {0xbfb0a658c4bd33d3ull, 0xbfa691a8442bff20ull}, {0xbfd023df6db940ffull, 0xbfc4874ae1ddd20cull},
    {0x3fe4540357a35504ull, 0x3fe1b3eb5385acacull}, {0x3fe52896dd26b724ull, 0x3fe29ad9bc844e5aull},
    {0x3fdde1eb4202d9cfull, 0x3fd875247d7b377cull}, {0xbfa73d188f42fe82ull, 0xbf9fb6d136058080ull}};
  static const uint64_t fy[][3] = {
    {0xc020680000000000ull, 0x4033f80000000000ull, 0xc041b7a7ddb96bd0ull},
    {0x402df80000000000ull, 0x40f28f5000000000ull, 0x406e5eaa7a486e52ull},
    {0xc03b940000000000ull, 0x40e2342000000000ull, 0xc07a2ce3d5fb7019ull},
    {0xc032fc0000000000ull, 0x4033f80000000000ull, 0xc0548066d1061a03ull},
    {0xc03f200000000000ull, 0x4033f80000000000ull, 0xc060ce64b7fd6aa8ull},
    {0x403df40000000000ull, 0x4033f80000000000ull, 0x40602c67ddc91485ull},
    {0x403cb00000000000ull, 0x4033f80000000000ull, 0x405efaeb02f8274eull},
    {0x403a140000000000ull, 0x40f28f5000000000ull, 0x407a6d566a2410fcull}};
  static uint64_t rec[BATCH][3];
  for (unsigned i = 0; !fyl2x && i < sizeof f2 / sizeof f2[0]; i++)
    CHECK_EQ(bits(fp80_to_f64(fp80_f2xm1(fp80_from_f64(dbl(f2[i][0]))))), f2[i][1]);
  for (unsigned i = 0; fyl2x && i < sizeof fy / sizeof fy[0]; i++)
    CHECK_EQ(bits(fp80_to_f64(fp80_fyl2x(fp80_from_f64(dbl(fy[i][1])), fp80_from_f64(dbl(fy[i][0]))))), fy[i][2]);
  /* The same vectors through the hooked oracle (the record layout of test_fyl2x / test_f2xm1). */
  fp80_set_cw(PINNED_CW);
  for (unsigned i = 0; i < BATCH; i++) {
    unsigned k = i % 8;
    rec[i][0] = fyl2x ? fy[k][0] : f2[k][0];
    rec[i][1] = fyl2x ? fy[k][1] : 0;
    rec[i][2] = 0;
  }
  run_x87(fyl2x ? g_code_fyl2x : g_code_f2xm1, PINNED_CW, rec);
  for (unsigned i = 0; i < 8; i++) CHECK_EQ(rec[i][2], fyl2x ? fy[i][2] : f2[i][1]);
}

static void test_fyl2x_equiv(void) {
  g_tr_exact = g_tr_total = 0;
  CHECK_EQ(test_fyl2x(PINNED_CW, N_PAIRS), 0);
  CHECK_EQ(test_fyl2x(0x137F, N_PAIRS_CW), 0); /* 64-bit precision, the word the CRT pow runs under */
  CHECK_EQ(g_tr_exact, g_tr_total);
  test_fp_vectors(1);
  printf("FYL2X cw=%04x/137f: hooked oracle vs C path, %u (x, y) pairs, %u bit-exact; reference vectors checked\n",
         PINNED_CW, g_tr_total, g_tr_exact);
}
static void test_f2xm1_equiv(void) {
  g_tr_exact = g_tr_total = 0;
  CHECK_EQ(test_f2xm1(PINNED_CW, N_PAIRS), 0);
  CHECK_EQ(test_f2xm1(0x137F, N_PAIRS_CW), 0);
  CHECK_EQ(g_tr_exact, g_tr_total);
  test_fp_vectors(0);
  printf("F2XM1 cw=%04x/137f: hooked oracle vs C path, %u operands, %u bit-exact; reference vectors checked\n",
         PINNED_CW, g_tr_total, g_tr_exact);
}

/* Integer test operand: ties, values around the int16/int32 limits, random magnitudes. */
static uint64_t rnd_int_operand(void) {
  uint64_t r = rnd();
  unsigned k = (unsigned)(r >> 60) & 7;
  double v;
  int64_t n = (int64_t)(rnd() % 200001) - 100000;
  switch (k) {
    case 0: v = (double)n + 0.5; break;                         /* tie */
    case 1: v = (double)(rnd() % 80001) - 40000.5; break;       /* int16 boundary area, tie */
    case 2: v = (double)(int64_t)(rnd() % 4294967296ull) - 2147483648.0 + 0.5; break; /* int32 range */
    case 3: v = dbl(rnd_bits()); break;
    case 4: v = (double)n / 8.0; break;
    case 5: v = (double)((int64_t)(rnd() % 5) - 2) * 2147483648.0 + (double)((int)(rnd() % 5) - 2) * 0.5; break;
    case 6: v = (double)((int)(rnd() % 7) - 3) * 32768.0 + (double)((int)(rnd() % 5) - 2) * 0.5; break;
    default: v = dbl(rnd_bits()) * 1e-3; break;
  }
  return bits(v);
}

static unsigned test_fistp(int wide, uint16_t cw, unsigned n) {
  unsigned bad = 0;
  uint64_t rec[BATCH][3];
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) { rec[i][0] = rnd_int_operand(); rec[i][1] = 0; rec[i][2] = 0; }
    run_x87(wide ? g_code_fistp32 : g_code_fistp16, cw, rec);
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t a = rec[i][0], x = rec[i][2];
      Fp80 v = fp80_from_f64(dbl(a));
      uint64_t f = wide ? (uint32_t)fp80_to_i32(v) : (uint16_t)fp80_to_i16(v);
      uint64_t xm = wide ? (x & 0xFFFFFFFFull) : (x & 0xFFFFull);
      if (xm != f && bad++ == 0)
        fprintf(stderr, "fistp%d mismatch cw %04x: %016llx x87 %llx fp80 %llx\n", wide ? 32 : 16, cw,
                (unsigned long long)a, (unsigned long long)xm, (unsigned long long)f);
    }
  }
  return bad;
}

/* ---- 3. ftol (34:0x0de6): the game's (long) cast, FISTP m64 under RC = 11b ---- */


static uint64_t rnd_ftol_operand(void) {
  static const uint64_t special[] = {
    0x41F0000000A00000ull,  /* 2^32 + 5: wraps to 5 */
    0xC1F0000000A00000ull,  /* -(2^32 + 5) */
    0x43E0000000000000ull,  /* 2^63: invalid, 0 */
    0xC3E0000000000000ull,  /* -2^63: exact, low word 0 */
    0x43DFFFFFFFFFFFFFull,  /* largest double below 2^63 */
    0x7FF8000000000000ull,  /* QNaN */
    0xFFF8000000000000ull,  /* real indefinite */
    0x7FF0000000000000ull, 0xFFF0000000000000ull,  /* +-inf */
    0x41DFFFFFFFC00000ull,  /* 2^31 - 1 */
    0x41E0000000000000ull,  /* 2^31 */
    0xC1E0000000000000ull,  /* -2^31 */
    0x3FEFFFFFFFFFFFFFull,  /* just below 1: truncates to 0 */
    0xBFEFFFFFFFFFFFFFull,
  };
  uint64_t r = rnd();
  unsigned k = (unsigned)(r >> 59) & 15;
  if (k == 0) return special[(r >> 20) % (sizeof special / sizeof special[0])];
  if (k == 1) return bits((double)(int64_t)rnd());                    /* up to 2^63 */
  if (k == 2) return bits((double)(int64_t)(rnd() >> 11) * 4.0);      /* wraps in 32 bits */
  if (k == 3) return (rnd() & 0x800FFFFFFFFFFFFFull) | (0x7FFull << 52) | 1; /* NaN, either sign, any payload */
  return rnd_int_operand();
}

static unsigned test_ftol(unsigned n) {
  unsigned bad = 0;
  uint64_t rec[BATCH][3];
  uint16_t cw = 0x0F7F; /* the original sets RC = 11b (truncate) around FISTP m64 */
  fp80_set_cw(cw);
  for (unsigned done = 0; done < n; done += BATCH) {
    for (unsigned i = 0; i < BATCH; i++) { rec[i][0] = rnd_ftol_operand(); rec[i][1] = 0; rec[i][2] = 0; }
    run_x87(g_code_fistp64, cw, rec);
    for (unsigned i = 0; i < BATCH; i++) {
      uint64_t a = rec[i][0];
      uint32_t x = (uint32_t)rec[i][2];
      uint32_t f = (uint32_t)fp80_to_i32_ftol(fp80_from_f64(dbl(a)));
      if (x != f && bad++ == 0)
        fprintf(stderr, "ftol mismatch: %016llx x87 %08x fp80 %08x\n", (unsigned long long)a, x, f);
    }
  }
  return bad;
}

static void test_ftol_cases(void) {
  fp80_set_cw(0x0F7F);
  CHECK_EQ(fp80_to_i32_ftol(fp80_from_f64(4294967301.0)), 5);          /* 2^32 + 5 wraps */
  CHECK_EQ(fp80_to_i32_ftol(fp80_from_f64(9223372036854775808.0)), 0); /* 2^63 */
  CHECK_EQ(fp80_to_i32_ftol(fp80_from_f64(-2.9)), -2);
  CHECK_EQ(fp80_to_i32_ftol(fp80_from_f64(2147483648.0)), INT32_MIN);  /* 2^31 wraps */
  CHECK_EQ(fp80_to_i32_ftol(fp80_from_f64(dbl(0x7FF8000000000000ull))), 0);
}

static void test_equivalence(void) {
  static const char *name[4] = {"FADD", "FSUB", "FMUL", "FDIV"};
  setup_code();
  for (int op = 0; op < 4; op++) {
    Stat s = test_arith(op, PINNED_CW, N_PAIRS);
    printf("%s cw=%04x: %u pairs, fp80 mismatches %u, plain-double mismatches %u", name[op], PINNED_CW,
           s.tested, s.mism_fp80, s.mism_double);
    if (s.mism_double)
      printf(" (e.g. %016llx op %016llx: x87 %016llx, double %016llx)", (unsigned long long)s.ex_a,
             (unsigned long long)s.ex_b, (unsigned long long)s.ex_x87, (unsigned long long)s.ex_dbl);
    printf("\n");
    CHECK_EQ(s.mism_fp80, 0);
    CHECK(s.mism_double > 0); /* the test can tell fp80 from plain double (regression proof) */
  }
  CHECK_EQ(test_fistp(0, PINNED_CW, N_PAIRS), 0);
  CHECK_EQ(test_fistp(1, PINNED_CW, N_PAIRS), 0);
  printf("FISTP m16/m32 cw=%04x: %d pairs each\n", PINNED_CW, N_PAIRS);
  CHECK_EQ(test_sqrt(PINNED_CW, N_PAIRS), 0);
  CHECK_EQ(test_sqrt(0x137F, N_PAIRS_CW), 0); /* 64-bit precision, the word the CRT sqrt runs under */
  CHECK_EQ(test_sqrt(0x027F, N_PAIRS_CW), 0); /* 53-bit precision */
  printf("FSQRT cw=%04x: %d operands\n", PINNED_CW, N_PAIRS);
  test_fyl2x_equiv();
  test_f2xm1_equiv();
  test_ftol_cases();
  CHECK_EQ(test_ftol(N_PAIRS), 0);
  printf("FISTP m64 (ftol) cw=0f7f: %d operands, low 32 bits\n", N_PAIRS);
}

/* The helpers honour the rounding control and the precision control too. */
static void test_other_control_words(void) {
  for (unsigned pc = 0; pc < 4; pc += 2) {   /* 24-bit and 53-bit; 01b is reserved */
    for (unsigned rc = 0; rc < 4; rc++) {
      uint16_t cw = (uint16_t)(0x037F | (pc << 8) | (rc << 10));
      for (int op = 0; op < 4; op++) CHECK_EQ(test_arith(op, cw, N_PAIRS_CW).mism_fp80, 0);
      CHECK_EQ(test_fistp(0, cw, N_PAIRS_CW), 0);
      CHECK_EQ(test_fistp(1, cw, N_PAIRS_CW), 0);
    }
  }
  for (unsigned rc = 1; rc < 4; rc++) {       /* 64-bit precision, directed rounding */
    uint16_t cw = (uint16_t)(0x037F | (rc << 10));
    for (int op = 0; op < 4; op++) CHECK_EQ(test_arith(op, cw, N_PAIRS_CW).mism_fp80, 0);
    CHECK_EQ(test_fistp(0, cw, N_PAIRS_CW), 0);
    CHECK_EQ(test_fistp(1, cw, N_PAIRS_CW), 0);
  }
}

/* ---- 4. Exceptional paths under FPU_CW_DEFAULT (0x1330: IM, DM, ZM, OM unmasked) ----
 * On the real machine an unmasked exception traps to the game's handler at 34:0x1db4.
 * Unicorn's x87 does NOT deliver the trap: it computes the IEEE result and only sets the
 * status flags (ES and the sticky bits). So the harness checks the status word after a
 * top-level harness_call (opt-in harness_set_fp_trap_check) and records an error, and
 * fp80 reports the same exceptions through fp80_exceptions() and a trap handler.
 * MEASURED GAP: the Unicorn x87 sets a status flag only for division by zero (and also
 * for 0/0, where a real x87 sets IE). It sets NOTHING for inf-inf, overflow at FSTP m64
 * or a denormal operand, so the harness check cannot see those traps; fp80 can. */
#define X_IE 0x01u
#define X_DE 0x02u
#define X_ZE 0x04u
#define X_OE 0x08u

static unsigned g_trap_calls;
static uint16_t g_trap_last;
static void on_fp80_trap(uint16_t unmasked) { g_trap_calls++; g_trap_last = unmasked; }

/* One FADD/FSUB/FMUL/FDIV of a and b (doubles, stored with FSTP m64) on the Unicorn FPU. */
static int unicorn_exc(int op, uint64_t a, uint64_t b, uint16_t cw, uint16_t *pending) {
  uint64_t rec[BATCH][3];
  Regs16 io;
  memset(rec, 0, sizeof rec);
  rec[0][0] = a; rec[0][1] = b;
  memcpy(LIN((uint32_t)g_data << 16), rec, BATCH * 24u);
  memset(&io, 0, sizeof io);
  io.es = g_data; io.si = 0; io.cx = 1;
  harness_set_fp_trap_check(true);
  harness_set_fpcw(cw);
  harness_set_fpsw(0);
  harness_set_quiet(true);
  harness_call(g_code_arith[op], 0, HCONV_CDECL, NULL, 0, &io);
  harness_set_quiet(false);
  harness_set_fp_trap_check(false);
  *pending = harness_fp_trap_pending();
  return harness_last_error();
}

/* The same operation in fp80: exceptions raised (x87 layout) and the trap handler calls. */
static uint16_t fp80_exc(int op, uint64_t a, uint64_t b, uint16_t cw) {
  fp80_set_cw(cw);
  fp80_clear_exceptions();
  g_trap_calls = 0;
  fp80_set_trap_handler(on_fp80_trap);
  (void)fp80_to_f64(fp80_op(op, fp80_from_f64(dbl(a)), fp80_from_f64(dbl(b))));
  fp80_set_trap_handler(NULL);
  return fp80_exceptions();
}

static void test_exceptional_paths(void) {
  struct { const char *name; int op; uint64_t a, b; uint16_t want; bool uni; } t[] = {
    {"divide by zero 1/0",   3, 0x3FF0000000000000ull, 0, X_ZE, true},
    {"invalid 0/0",          3, 0, 0, X_IE, true}, /* Unicorn reports ZE for 0/0 (real x87: IE) */
    {"invalid inf-inf",      1, 0x7FF0000000000000ull, 0x7FF0000000000000ull, X_IE, false},
    {"overflow at FSTP m64", 2, 0x7FE0000000000000ull, 0x4010000000000000ull, X_OE, false},
    {"denormal operand",     0, 1, 0x3FF0000000000000ull, X_DE, false},
  };
  setup_code();
  for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
    uint16_t pend, e = fp80_exc(t[i].op, t[i].a, t[i].b, FPU_CW_DEFAULT);
    /* fp80: the intended exception is raised and, being unmasked, trapped */
    CHECK(e & t[i].want);
    if (!(e & t[i].want)) fprintf(stderr, "  case: %s\n", t[i].name);
    CHECK_EQ(g_trap_calls, 1);
    CHECK(g_trap_last & t[i].want);
    CHECK_EQ(g_trap_last & ~0x3Fu, 0);
    /* masked (0x037F): the same flags are raised but no trap is delivered */
    e = fp80_exc(t[i].op, t[i].a, t[i].b, PINNED_CW);
    CHECK(e & t[i].want);
    CHECK_EQ(g_trap_calls, 0);
    /* Unicorn at 0x1330: recorded error, never a silent success */
    if (t[i].uni) {
      CHECK(unicorn_exc(t[i].op, t[i].a, t[i].b, FPU_CW_DEFAULT, &pend) != 0);
      CHECK(pend != 0);
    } else { /* Unicorn raises no flag at all: the harness cannot see this trap (documented gap) */
      CHECK_EQ(unicorn_exc(t[i].op, t[i].a, t[i].b, FPU_CW_DEFAULT, &pend), 0);
      CHECK_EQ(pend, 0);
    }
    /* Unicorn masked: no error */
    CHECK_EQ(unicorn_exc(t[i].op, t[i].a, t[i].b, PINNED_CW, &pend), 0);
    CHECK_EQ(pend, 0);
  }
  /* A normal operation under 0x1330 is not an error (only the masked PE flag is set). */
  {
    uint16_t pend;
    CHECK_EQ(unicorn_exc(0, 0x3FF0000000000000ull, 0x4000000000000000ull, FPU_CW_DEFAULT, &pend), 0);
    CHECK_EQ(pend, 0);
    CHECK_EQ(fp80_exc(0, 0x3FF0000000000000ull, 0x4000000000000000ull, FPU_CW_DEFAULT) & 0x0F, 0);
    CHECK_EQ(g_trap_calls, 0);
  }
  fp80_set_cw(PINNED_CW);
  harness_set_fpcw(PINNED_CW);
}

int main(void) {
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  test_default_control_word();
  test_control_word_at_winmain();
  test_control_word_sentinel();
  test_equivalence();
  test_other_control_words();
  test_exceptional_paths();
  test_default_control_word_registered();
  harness_shutdown();
  return TEST_DONE();
}
