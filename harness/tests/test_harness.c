/* Tests for the Unicorn harness (harness/harness.c). Task stars-dwf.2. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "test.h"

#define MB (1024u * 1024u)

/* ---- Spike: nested uc_emu_start from an INT hook, and INT without an IDT ---- */

static void sp_desc(uint8_t *d, uint32_t base, uint32_t limit, uint8_t access) {
  d[0] = limit & 0xFF; d[1] = (limit >> 8) & 0xFF;
  d[2] = base & 0xFF; d[3] = (base >> 8) & 0xFF; d[4] = (base >> 16) & 0xFF;
  d[5] = access; d[6] = (limit >> 16) & 0x0F; d[7] = (base >> 24) & 0xFF;
}

typedef struct {
  int fired;
  uint32_t intno;
  int nested_rc;
  uint16_t cs_in_hook;
  uint16_t ip_in_hook;
} SpikeState;

static void spike_intr(uc_engine *uc, uint32_t intno, void *ud) {
  SpikeState *s = ud;
  uint32_t eip = 0;
  uint16_t cs = 0;
  s->fired++;
  s->intno = intno;
  uc_reg_read(uc, UC_X86_REG_EIP, &eip);
  s->ip_in_hook = (uint16_t)eip;
  uc_reg_read(uc, UC_X86_REG_CS, &cs);
  s->cs_in_hook = cs;
  /* Nested run: save outer context, run block B (code selector 0x08, base 0x4000)
   * until its HLT, restore the outer context. */
  uc_context *ctx = NULL;
  uc_context_alloc(uc, &ctx);
  uc_context_save(uc, ctx);
  s->nested_rc = (int)uc_emu_start(uc, 0x200000 + 0x4000, 0x200000 + 0x4000 + 9, 0, 0);
  uc_context_restore(uc, ctx);
  uc_context_free(ctx);
}

static void test_spike_nested_and_int_hook(void) {
  /* Real-mode boot at 0x500, same recipe as tests/test_unicorn.c. */
  static const uint8_t boot[] = {
      0x0f, 0x01, 0x16, 0x20, 0x05,       /* LGDT [0x520]  */
      0x0f, 0x20, 0xc0, 0x0c, 0x01,       /* MOV EAX,CR0; OR AL,1 */
      0x0f, 0x22, 0xc0,                   /* MOV CR0,EAX   */
      0xea, 0x00, 0x00, 0x08, 0x00,       /* JMP FAR 8:0   */
  };
  /* Outer code at selector 8 offset 0 (base 0x200000). */
  static const uint8_t outer[] = {
      0xb8, 0x10, 0x00,                   /* 00 MOV AX,0x10      */
      0x8e, 0xd8,                         /* 03 MOV DS,AX        */
      0xbb, 0x11, 0x11,                   /* 05 MOV BX,0x1111    */
      0xcd, 0xfe,                         /* 08 INT 0xFE         */
      0x89, 0x1e, 0x02, 0x00,             /* 0A MOV [2],BX       */
      0xf4,                               /* 0E HLT              */
  };
  /* Inner code at offset 0x4000: sets BX, writes marker word, then reaches the
   * stop address 0x4006 (a NOP). */
  static const uint8_t inner[] = {
      0xbb, 0x22, 0x22,                   /* MOV BX,0x2222 (must be undone by restore) */
      0xc7, 0x06, 0x00, 0x00, 0xcd, 0xab, /* MOV word [0],0xABCD -- placed after stop? */
  };
  uint8_t gdt[24], gdtr[6] = {0x17, 0, 0x00, 0x10, 0, 0};
  uc_engine *uc = NULL;
  SpikeState st;
  uc_hook h;
  uint8_t b[2];
  uint16_t marker = 0, after = 0;

  memset(&st, 0, sizeof st);
  memset(gdt, 0, sizeof gdt);
  sp_desc(gdt + 8, 0x200000, 0xFFFF, 0x9B);
  sp_desc(gdt + 16, 0x300000, 0xFFFF, 0x93);
  CHECK_EQ(uc_open(UC_ARCH_X86, UC_MODE_16, &uc), UC_ERR_OK);
  CHECK_EQ(uc_mem_map(uc, 0, 4 * MB, UC_PROT_ALL), UC_ERR_OK);
  uc_mem_write(uc, 0x1000, gdt, sizeof gdt);
  gdtr[2] = 0x00; gdtr[3] = 0x10;
  uc_mem_write(uc, 0x520, gdtr, 6);
  uc_mem_write(uc, 0x500, boot, sizeof boot);
  uc_mem_write(uc, 0x200000, outer, sizeof outer);
  /* inner block: MOV BX,0x2222 ; MOV word [0],0xABCD with DS=0x10 ; stop at +0x0C */
  uc_mem_write(uc, 0x200000 + 0x4000, inner, sizeof inner);
  {
    uint16_t z = 0;
    uc_reg_write(uc, UC_X86_REG_CS, &z);
    uc_reg_write(uc, UC_X86_REG_DS, &z);
  }
  uc_hook_add(uc, &h, UC_HOOK_INTR, (void *)spike_intr, &st, 1, 0);
  CHECK_EQ(uc_emu_start(uc, 0x500, 0x200000 + 0x0E, 0, 0), UC_ERR_OK);
  uc_mem_read(uc, 0x300000 + 0, b, 2); marker = (uint16_t)(b[0] | (b[1] << 8));
  uc_mem_read(uc, 0x300000 + 2, b, 2); after = (uint16_t)(b[0] | (b[1] << 8));
  CHECK_EQ(st.fired, 1);              /* hook fires without an IDT */
  CHECK_EQ(st.intno, 0xFE);
  CHECK_EQ(st.ip_in_hook, 0x000A);    /* IP is already past the INT */
  CHECK_EQ(st.nested_rc, UC_ERR_OK);  /* nested uc_emu_start works */
  CHECK_EQ(marker, 0xABCD);           /* inner block ran */
  CHECK_EQ(after, 0x1111);            /* outer resumed after the INT with BX restored */
  uc_close(uc);
}

/* ---- Harness tests ---- */

#include "harness/harness.h"
#include "runtime/mem.h"

#define EXE "original/stars26i.exe"
#define SCRATCH 0xC000u /* DGROUP offset inside the (unused) heap area, for test words */

static uint16_t rdw(const void *p) {
  uint16_t v;
  memcpy(&v, p, 2);
  return v;
}
static uint16_t dg16(uint16_t off) { return rdw(LINN(off)); }
static uint16_t seg16(uint16_t seg, uint16_t off) { return rdw(LIN(MK_FP(seg << 3, off))); }

static void test_loader(void) {
  const uint8_t *p = LIN(MK_FP(5 << 3, 0x1586));
  static const uint8_t want[4] = {0x9B, 0x26, 0xDB, 0x47};
  uint32_t fp;
  /* OSFIXUP sites keep their native x87 bytes. */
  CHECK_MEM(p, want, 4);
  /* KERNEL.114 __AHINCR site in segment 34 (nedump --relocs: 34:0x2053). */
  CHECK_EQ(seg16(34, 0x2053), 8);
  /* KERNEL.113 __AHSHIFT site (34:0x1211). */
  CHECK_EQ(seg16(34, 0x1211), 3);
  /* KERNEL.178 __WINFLAGS site (34:0x0010): the full harness value. */
  CHECK_EQ(seg16(34, 0x0010), WINFLAGS_DEFAULT);
  CHECK(WINFLAGS_DEFAULT & 0x01);
  /* DGROUP 0x1E7C: far pointer to the WIN87EM.1 thunk. */
  fp = (uint32_t)dg16(0x1E7C) | ((uint32_t)dg16(0x1E7E) << 16);
  CHECK_EQ(FP_SEG(fp), MEM_SEL_THUNK);
  CHECK_EQ(fp, harness_thunk_far("WIN87EM", 1));
  {
    const uint8_t *t = LIN(fp);
    CHECK_EQ(t[0], 0xCD);
    CHECK_EQ(t[1], 0xFE);
    CHECK_EQ(t[2], 0xCB); /* REGISTER with no argument bytes: plain RETF */
  }
  /* Internal SEGMENT fixup: DGROUP word 0x08DE is the selector of segment 9. */
  CHECK_EQ(dg16(0x08DE), 0x0048);
  /* Segment descriptors: code is 0x9B, DGROUP is data 0x93 and includes stack+heap. */
  CHECK_EQ(mem_gdt()[(34 << 3) + 5], 0x9B);
  CHECK_EQ(mem_gdt()[(35 << 3) + 5], 0x93);
  CHECK_EQ(desc_limit(MEM_SEL_DGROUP) + 1u, 26248u + 16384u + 8192u);
  CHECK_EQ(harness_stack_top(), 26248 + 16384);
}

static void test_thunk_table(void) {
  const char *mod = NULL, *prev_mod = NULL;
  uint16_t ord = 0, prev_ord = 0;
  size_t n = harness_import_count();
  /* NE module-table order, then ordinal ascending: modules appear in
   * contiguous groups and ordinals ascend within a group. */
  CHECK_EQ(n, 197);
  for (size_t i = 0; i < n; i++) {
    CHECK(harness_import_info(i, &mod, &ord, NULL));
    if (prev_mod && strcmp(prev_mod, mod) == 0) CHECK(ord > prev_ord);
    prev_mod = mod;
    prev_ord = ord;
  }
  /* WIN87EM is module 1 of the exe, so its thunk is the first one. */
  CHECK_EQ(harness_thunk_far("WIN87EM", 1), MK_FP(MEM_SEL_THUNK, 0));
  CHECK_EQ(harness_thunk_far("KERNEL", 91) >> 16, MEM_SEL_THUNK);
  CHECK_EQ(harness_thunk_far("NOSUCH", 1), 0);
  /* KERNEL.91 InitTask is REGISTER with no argument bytes. */
  {
    const uint8_t *t = LIN(harness_thunk_far("KERNEL", 91));
    CHECK_EQ(t[0], 0xCD);
    CHECK_EQ(t[2], 0xCB);
  }
  /* USER.1 MessageBox pops 12 argument bytes: CA 0C 00. */
  {
    uint32_t mb = harness_thunk_far("USER", 1);
    if (mb) {
      const uint8_t *t = LIN(mb);
      CHECK_EQ(t[2], 0xCA);
      CHECK_EQ(t[3], 12);
      CHECK_EQ(t[4], 0);
    }
  }
}

static void test_entry_first_api(void) {
  Regs16 r;
  harness_log_clear();
  harness_set_quiet(true);
  harness_stop_after_api(1);
  harness_call(34 << 3, 0x001A, HCONV_CDECL, NULL, 0, NULL);
  harness_stop_after_api(0);
  CHECK(harness_stopped_early());
  CHECK_EQ(harness_log_count(), 1);
  if (harness_log_count() >= 1) {
    const HLogEntry *e = harness_log_get(0);
    CHECK(strcmp(e->module, "KERNEL") == 0);
    CHECK_EQ(e->ordinal, 91);
    CHECK(e->name && strcmp(e->name, "InitTask") == 0);
    CHECK(!e->implemented);
  }
  harness_regs(&r);
  CHECK_EQ(r.bp, 0);
  CHECK_EQ(r.cs, MEM_SEL_THUNK);
  harness_set_quiet(false);
}

/* Test-only handlers. */
static uint16_t g_pop_sp, g_pop_words[6];
static void h_pascal12(HCall *c) {
  g_pop_sp = c->regs->sp;
  for (unsigned i = 0; i < 6; i++) g_pop_words[i] = hcall_word(c, i);
  c->ret = 0xBEEF;
}

static void test_pascal_pop(void) {
  /* mov ax,1..6 ; push ax ; call far import ; (result AX saved) ; retf */
  uint8_t code[64];
  size_t n = 0;
  uint32_t imp = harness_add_import("TESTMOD", 1, "PascalSix", HK_PASCAL, 12, HR_RET16, h_pascal12);
  Regs16 io;
  uint16_t sel;
  CHECK(imp != 0);
  for (uint8_t v = 1; v <= 6; v++) {
    code[n++] = 0xB8; code[n++] = v; code[n++] = 0;  /* MOV AX,v */
    code[n++] = 0x50;                                 /* PUSH AX  */
  }
  code[n++] = 0x9A;                                   /* CALL FAR import */
  code[n++] = (uint8_t)(imp & 0xFF); code[n++] = (uint8_t)((imp >> 8) & 0xFF);
  code[n++] = (uint8_t)((imp >> 16) & 0xFF); code[n++] = (uint8_t)(imp >> 24);
  code[n++] = 0xCB;                                   /* RETF */
  sel = harness_alloc_code(code, n);
  CHECK(sel != 0);
  memset(&io, 0, sizeof io);
  harness_log_clear();
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(io.ax, 0xBEEF);
  CHECK_EQ(io.sp, harness_stack_top());               /* SP after the return == SP before the pushes */
  /* Inside the handler: 12 arg bytes + far return (4) + harness return (4) below the top. */
  CHECK_EQ(g_pop_sp, harness_stack_top() - 4 - 12 - 4);
  CHECK_EQ(g_pop_words[0], 6);                        /* last pushed word first */
  CHECK_EQ(g_pop_words[5], 1);
  CHECK_EQ(harness_log_count(), 1);
  CHECK_EQ(harness_log_get(0)->argbytes, 12);
  CHECK_EQ(harness_log_get(0)->args[0], 6);
  CHECK_EQ(harness_log_get(0)->ret, 0xBEEF);
  CHECK(harness_log_get(0)->implemented);
}

static uint16_t g_cb_sel;
static void h_callback(HCall *c) {
  (void)c;
  harness_call(g_cb_sel, 0, HCONV_CDECL, NULL, 0, NULL);
}

static void test_callback(void) {
  /* Callback body (entered with DS = SS = DGROUP):
   *   inc word [SCRATCH]; mov ax,ds; mov [SCRATCH+2],ax; mov ax,ss; mov [SCRATCH+4],ax;
   *   mov bx,0x9999 (must not leak to the outer code); retf */
  const uint8_t cb[] = {
      0xFF, 0x06, SCRATCH & 0xFF, SCRATCH >> 8,
      0x8C, 0xD8, 0xA3, (SCRATCH + 2) & 0xFF, (SCRATCH + 2) >> 8,
      0x8C, 0xD0, 0xA3, (SCRATCH + 4) & 0xFF, (SCRATCH + 4) >> 8,
      0xBB, 0x99, 0x99, 0xCB,
  };
  uint32_t imp = harness_add_import("TESTMOD", 2, "Callback", HK_PASCAL, 0, HR_NONE, h_callback);
  uint8_t code[32];
  size_t n = 0;
  Regs16 io;
  uint16_t sel;
  g_cb_sel = harness_alloc_code(cb, sizeof cb);
  CHECK(g_cb_sel != 0);
  code[n++] = 0xBB; code[n++] = 0x34; code[n++] = 0x12;  /* MOV BX,0x1234 */
  code[n++] = 0x9A;
  code[n++] = (uint8_t)(imp & 0xFF); code[n++] = (uint8_t)((imp >> 8) & 0xFF);
  code[n++] = (uint8_t)((imp >> 16) & 0xFF); code[n++] = (uint8_t)(imp >> 24);
  code[n++] = 0x89; code[n++] = 0x1E;                     /* MOV [SCRATCH+6],BX */
  code[n++] = (SCRATCH + 6) & 0xFF; code[n++] = (SCRATCH + 6) >> 8;
  code[n++] = 0xB8; code[n++] = 0x77; code[n++] = 0x00;  /* MOV AX,0x77 */
  code[n++] = 0xCB;
  sel = harness_alloc_code(code, n);
  *(uint16_t *)LINN(SCRATCH) = 41;
  memset(&io, 0, sizeof io);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(dg16(SCRATCH), 42);                 /* callback incremented the word */
  CHECK_EQ(dg16(SCRATCH + 2), MEM_SEL_DGROUP); /* DS = DGROUP in the callback */
  CHECK_EQ(dg16(SCRATCH + 4), MEM_SEL_DGROUP); /* SS = DGROUP in the callback */
  CHECK_EQ(dg16(SCRATCH + 6), 0x1234);         /* outer BX survived the nested run */
  CHECK_EQ(io.ax, 0x77);                       /* outer code continued after the call */
  CHECK_EQ(io.sp, harness_stack_top());
}

/* Repeated top-level calls that each make a nested call. Before the fix the
 * second one crashed inside Unicorn (a stale translation block after the
 * nested run), so every nested call must leave a clean translation cache. */
static void test_callback_repeated(void) {
  const uint8_t cb[] = {0xFF, 0x06, (SCRATCH + 8) & 0xFF, (SCRATCH + 8) >> 8, 0xCB}; /* inc word [SCRATCH+8]; retf */
  uint32_t imp = harness_add_import("TESTMOD", 5, "CallbackAgain", HK_PASCAL, 0, HR_NONE, h_callback);
  uint8_t code[8] = {0x9A, (uint8_t)(imp & 0xFF), (uint8_t)((imp >> 8) & 0xFF), (uint8_t)((imp >> 16) & 0xFF),
                     (uint8_t)(imp >> 24), 0xB8, 0x07, 0x00}; /* CALL FAR imp; MOV AX,7; (RETF below) */
  uint8_t full[9];
  uint16_t sel;
  memcpy(full, code, 8);
  full[8] = 0xCB;
  g_cb_sel = harness_alloc_code(cb, sizeof cb);
  sel = harness_alloc_code(full, sizeof full);
  *(uint16_t *)LINN(SCRATCH + 8) = 0;
  for (int i = 0; i < 6; i++) {
    CHECK_EQ(harness_call(sel, 0, HCONV_PASCAL, NULL, 0, NULL) & 0xFFFF, 7);
    CHECK_EQ(harness_last_error(), 0);
  }
  CHECK_EQ(dg16(SCRATCH + 8), 6);
}

static void test_cdecl_args(void) {
  /* near-style cdecl callee: mov ax,[sp+4]; add ax,[sp+6]; retf  (BP-less) */
  const uint8_t code[] = {
      0x89, 0xE3,             /* MOV BX,SP        */
      0x36, 0x8B, 0x47, 0x04, /* MOV AX,SS:[BX+4] */
      0x36, 0x03, 0x47, 0x06, /* ADD AX,SS:[BX+6] */
      0xCB,
  };
  uint16_t sel = harness_alloc_code(code, sizeof code);
  uint16_t args[2] = {30, 12};
  Regs16 io;
  memset(&io, 0, sizeof io);
  CHECK_EQ(harness_call(sel, 0, HCONV_CDECL, args, 2, &io) & 0xFFFF, 42);
  CHECK_EQ(io.sp, harness_stack_top()); /* caller popped the cdecl arguments */
}

/* REGISTER handler that returns new ES/DS and flag bits (defects 1 and 2). */
static int g_reg_mode;
static void h_regs(HCall *c) {
  if (g_reg_mode == 0) {
    c->regs->es = MEM_SEL_DGROUP;
    c->regs->ds = 5 << 3;
  } else if (g_reg_mode == 1) {
    c->regs->flags |= 1; /* set CF */
  } else {
    c->regs->flags &= (uint16_t)~1u; /* clear CF */
  }
}

static uint16_t reg_caller(const uint8_t *tail, size_t tail_n, uint32_t imp, const uint8_t *head,
                           size_t head_n) {
  uint8_t code[48];
  size_t n = 0;
  memcpy(code, head, head_n);
  n += head_n;
  code[n++] = 0x9A;
  code[n++] = (uint8_t)(imp & 0xFF); code[n++] = (uint8_t)((imp >> 8) & 0xFF);
  code[n++] = (uint8_t)((imp >> 16) & 0xFF); code[n++] = (uint8_t)(imp >> 24);
  memcpy(code + n, tail, tail_n);
  n += tail_n;
  return harness_alloc_code(code, n);
}

static void test_handler_segment_regs(void) {
  uint32_t imp = harness_add_import("TESTMOD", 3, "SetSegs", HK_REGISTER, 0, HR_NONE, h_regs);
  static const uint8_t tail[] = {
      0x26, 0xA1, 0x10, 0xC0,       /* MOV AX,ES:[SCRATCH+0x10] */
      0x8B, 0x1E, 0x86, 0x15,       /* MOV BX,DS:[0x1586]       */
      0xCB,
  };
  uint16_t sel = reg_caller(tail, sizeof tail, imp, NULL, 0);
  Regs16 io;
  *(uint16_t *)LINN(SCRATCH + 0x10) = 0xABCD;
  g_reg_mode = 0;
  memset(&io, 0, sizeof io);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(io.ax, 0xABCD);                          /* ES descriptor cache loaded */
  CHECK_EQ(io.bx, seg16(5, 0x1586));                /* DS descriptor cache loaded */
  CHECK_EQ(io.es, MEM_SEL_DGROUP);
  CHECK_EQ(io.ds, 5 << 3);
  CHECK_EQ(io.sp, harness_stack_top());
}

static void test_handler_flags(void) {
  uint32_t imp = harness_add_import("TESTMOD", 4, "SetFlags", HK_REGISTER, 0, HR_NONE, h_regs);
  static const uint8_t head_set[] = {0xB8, 0x00, 0x00};             /* MOV AX,0 */
  static const uint8_t head_clr[] = {0xB8, 0x00, 0x00, 0xF9};       /* MOV AX,0; STC */
  static const uint8_t tail[] = {0x15, 0x00, 0x00, 0xCB};           /* ADC AX,0; RETF */
  Regs16 io;
  uint16_t sel = reg_caller(tail, sizeof tail, imp, head_set, sizeof head_set);
  g_reg_mode = 1;
  memset(&io, 0, sizeof io);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(io.ax, 1); /* handler-set CF seen by the caller */
  sel = reg_caller(tail, sizeof tail, imp, head_clr, sizeof head_clr);
  g_reg_mode = 2;
  memset(&io, 0, sizeof io);
  harness_call(sel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK_EQ(io.ax, 0); /* handler-cleared CF seen by the caller */
}

static void test_fault_recovery(void) {
  static const uint8_t bad[] = {0xEA, 0x00, 0x00, 0xF8, 0x7F}; /* JMP FAR 7FF8:0000 (no descriptor) */
  const uint8_t good[] = {0xB8, 0x2A, 0x00, 0xCB};             /* MOV AX,42; RETF */
  uint16_t bsel = harness_alloc_code(bad, sizeof bad);
  uint16_t gsel = harness_alloc_code(good, sizeof good);
  Regs16 io;
  harness_set_quiet(true);
  memset(&io, 0, sizeof io);
  harness_call(bsel, 0, HCONV_CDECL, NULL, 0, &io);
  CHECK(harness_last_error() != 0);
  memset(&io, 0, sizeof io);
  CHECK_EQ(harness_call(gsel, 0, HCONV_CDECL, NULL, 0, &io) & 0xFFFF, 42);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(io.sp, harness_stack_top());
  harness_set_quiet(false);
}


/* stars-dwf.3 fix 3: a NEAR callee in a segment whose size is a multiple of 16
 * far-calls the segment allocated right after it; the stop address (just past
 * the limit) must not be the next segment's offset 0. */
static void test_near_call_next_segment_adjacent(void) {
  uint8_t a_code[16] = {0x9A, 0x00, 0x00, 0x00, 0x00, 0xC3}; /* CALL FAR b:0; RET */
  static const uint8_t b_code[] = {0xB8, 0x2A, 0x00, 0xCB};  /* MOV AX,42; RETF */
  uint16_t a = 0, b;
  uint32_t r;
  /* allocate b first is not possible: a must call b, so patch the selector in afterwards */
  a = harness_alloc_code(a_code, sizeof a_code);
  b = harness_alloc_code(b_code, sizeof b_code);
  CHECK(a != 0 && b != 0);
  memcpy(mem_arena() + desc_base(a) + 3, &b, 2);
  harness_set_quiet(true);
  r = harness_call(a, 0, HCONV_NEAR_CDECL, NULL, 0, NULL);
  harness_set_quiet(false);
  CHECK_EQ(harness_last_error(), 0);
  CHECK_EQ(r & 0xFFFF, 42);
}

/* A full 64 KB code segment cannot be called NEAR (stop offset 0xFFFF is inside it). */
static void test_near_call_64k_rejected(void) {
  static uint8_t big[65536];
  uint16_t s;
  big[0] = 0xC3;
  s = harness_alloc_code(big, sizeof big);
  CHECK(s != 0);
  harness_set_quiet(true);
  harness_call(s, 0, HCONV_NEAR_CDECL, NULL, 0, NULL);
  harness_set_quiet(false);
  CHECK(harness_last_error() != 0);
}

int main(void) {
  test_spike_nested_and_int_hook();
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  test_loader();
  test_thunk_table();
  test_entry_first_api();
  test_pascal_pop();
  test_callback();
  test_callback_repeated();
  test_cdecl_args();
  test_handler_segment_regs();
  test_handler_flags();
  test_fault_recovery();
  test_near_call_next_segment_adjacent();
  test_near_call_64k_rejected();
  harness_shutdown();
  return TEST_DONE();
}
