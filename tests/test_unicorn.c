/* Re-checks the three Unicorn 2.1.4 premises that the test oracle relies on
 * (see docs/PLAN.md section 2, tool 4):
 *   1. 16-bit real-mode x87 works (round to even, FNINIT control word 0x037F).
 *   2. 16-bit protected mode works with GDT descriptors based above 1 MB,
 *      entered through LGDT, CR0.PE and a far jump.
 * Unicorn gotchas that the code below follows:
 *   - CS must be loaded by a far jump in protected mode. Writing UC_X86_REG_CS
 *     directly does not load a descriptor there.
 *   - In 16-bit mode, uc_emu_start takes LINEAR begin/until addresses.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unicorn/unicorn.h>

#include "test.h"

#define MB (1024u * 1024u)

/* Opens a 16-bit x86 engine with [0, size) mapped read/write/execute. */
static uc_engine *new_engine(uint32_t size) {
  uc_engine *uc = NULL;
  CHECK_EQ(uc_open(UC_ARCH_X86, UC_MODE_16, &uc), UC_ERR_OK);
  CHECK_EQ(uc_mem_map(uc, 0, size, UC_PROT_ALL), UC_ERR_OK);
  return uc;
}

static void wr_bytes(uc_engine *uc, uint32_t addr, const void *buf, size_t n) {
  CHECK_EQ(uc_mem_write(uc, addr, buf, n), UC_ERR_OK);
}

static void wr16(uc_engine *uc, uint32_t addr, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v & 0xFF), (uint8_t)(v >> 8)};
  wr_bytes(uc, addr, b, 2);
}

static uint16_t rd16(uc_engine *uc, uint32_t addr) {
  uint8_t b[2] = {0, 0};
  CHECK_EQ(uc_mem_read(uc, addr, b, 2), UC_ERR_OK);
  return (uint16_t)(b[0] | (b[1] << 8));
}

/* Writes an IEEE 754 double, little-endian (the x87 memory format for qword). */
static void wr_f64(uc_engine *uc, uint32_t addr, double d) {
  uint64_t bits;
  uint8_t b[8];
  int i;
  memcpy(&bits, &d, sizeof bits);
  for (i = 0; i < 8; i++) {
    b[i] = (uint8_t)(bits >> (8 * i));
  }
  wr_bytes(uc, addr, b, 8);
}

static void set_seg(uc_engine *uc, int reg, uint16_t v) {
  CHECK_EQ(uc_reg_write(uc, reg, &v), UC_ERR_OK);
}

/* Builds an 8-byte GDT descriptor with flags 0 (byte granularity). */
static void make_desc(uint8_t out[8], uint32_t base, uint32_t limit, uint8_t access) {
  out[0] = (uint8_t)(limit & 0xFF);
  out[1] = (uint8_t)((limit >> 8) & 0xFF);
  out[2] = (uint8_t)(base & 0xFF);
  out[3] = (uint8_t)((base >> 8) & 0xFF);
  out[4] = (uint8_t)((base >> 16) & 0xFF);
  out[5] = access;
  out[6] = (uint8_t)((limit >> 16) & 0x0F);
  out[7] = (uint8_t)((base >> 24) & 0xFF);
}

static void test_real_mode_x87_round_to_even(void) {
  static const uint8_t code[] = {
      0x9b, 0xdd, 0x06, 0x00, 0x00, /* FWAIT; FLD qword [0]      */
      0x9b, 0xdc, 0x0e, 0x08, 0x00, /* FWAIT; FMUL qword [8]     */
      0x9b, 0xdf, 0x1e, 0x10, 0x00, /* FWAIT; FISTP word [0x10]  */
      0x9b,                         /* FWAIT                     */
  };
  uc_engine *uc = new_engine(MB);

  wr_bytes(uc, 0x10000, code, sizeof code);
  wr_f64(uc, 0x20000, 1.5);
  wr_f64(uc, 0x20008, 3.0);
  wr16(uc, 0x20010, 0xFFFF);
  set_seg(uc, UC_X86_REG_CS, 0x1000);
  set_seg(uc, UC_X86_REG_DS, 0x2000);

  /* 1.5 * 3.0 = 4.5, which rounds to even (4). */
  CHECK_EQ(uc_emu_start(uc, 0x10000, 0x10010, 0, 0), UC_ERR_OK);
  CHECK_EQ(rd16(uc, 0x20010), 4);
  uc_close(uc);
}

static void test_real_mode_fninit_control_word(void) {
  static const uint8_t code[] = {
      0xdb, 0xe3,             /* FNINIT             */
      0xd9, 0x3e, 0x00, 0x00, /* FNSTCW [0]         */
  };
  uc_engine *uc = new_engine(MB);

  wr_bytes(uc, 0x10000, code, sizeof code);
  wr16(uc, 0x20000, 0x1234); /* sentinel, so a no-op store is detected */
  set_seg(uc, UC_X86_REG_CS, 0x1000);
  set_seg(uc, UC_X86_REG_DS, 0x2000);

  CHECK_EQ(uc_emu_start(uc, 0x10000, 0x10006, 0, 0), UC_ERR_OK);
  CHECK_EQ(rd16(uc, 0x20000), 0x037F);
  uc_close(uc);
}

static void test_protected_mode_descriptor_above_1mb(void) {
  /* Boot code at 0x7C00 (real mode, CS=0, DS=0). */
  static const uint8_t boot[] = {
      0x0f, 0x01, 0x16, 0x20, 0x7c,       /* LGDT [0x7C20]            */
      0x0f, 0x20, 0xc0,                   /* MOV EAX, CR0             */
      0x0c, 0x01,                         /* OR AL, 1                 */
      0x0f, 0x22, 0xc0,                   /* MOV CR0, EAX             */
      0xea, 0x00, 0x00, 0x08, 0x00,       /* JMP FAR 0x0008:0000      */
  };
  /* Payload at 0x200000 (code selector 0x08, base 0x200000). */
  static const uint8_t payload[] = {
      0xb8, 0x10, 0x00,                   /* MOV AX, 0x10             */
      0x8e, 0xd8,                         /* MOV DS, AX  (data1)      */
      0x8e, 0xd0,                         /* MOV SS, AX                */
      0xbc, 0xf0, 0xff,                   /* MOV SP, 0xFFF0           */
      0xa1, 0x00, 0x00,                   /* MOV AX, [0]  -> 0x0010   */
      0x83, 0xc0, 0x08,                   /* ADD AX, 8    -> 0x0018   */
      0x8e, 0xc0,                         /* MOV ES, AX  (data2)      */
      0x26, 0xa1, 0x00, 0x00,             /* MOV AX, ES:[0] -> 0xBEEF */
      0xa3, 0x02, 0x00,                   /* MOV [2], AX              */
      0xf4,                               /* HLT                      */
  };
  /* GDT at 0x1000: null, code (base 0x200000), data1 (base 0x300000),
   * data2 (base 0x310000). */
  uint8_t gdt[32];
  uint8_t data2[8];
  uint8_t data2_mem[8];
  static const uint8_t expect_data2[8] = {0xff, 0xff, 0x00, 0x00,
                                          0x31, 0x93, 0x00, 0x00};
  static const uint8_t gdtr[6] = {0x1F, 0x00, 0x00, 0x10, 0x00, 0x00};
  uc_engine *uc = new_engine(4 * MB);

  memset(gdt, 0, sizeof gdt);
  make_desc(gdt + 8, 0x200000, 0xFFFF, 0x9B);
  make_desc(gdt + 16, 0x300000, 0xFFFF, 0x93);
  make_desc(gdt + 24, 0x310000, 0xFFFF, 0x93);
  wr_bytes(uc, 0x1000, gdt, sizeof gdt);

  /* The descriptor builder and the literal bytes must agree... */
  make_desc(data2, 0x310000, 0xFFFF, 0x93);
  CHECK_MEM(data2, expect_data2, sizeof expect_data2);
  /* ...and the bytes actually in emulator memory at GDT[3] (0x1000 + 0x18). */
  CHECK_EQ(uc_mem_read(uc, 0x1000 + 0x18, data2_mem, sizeof data2_mem), UC_ERR_OK);
  CHECK_MEM(data2_mem, expect_data2, sizeof expect_data2);

  wr_bytes(uc, 0x7C20, gdtr, sizeof gdtr);
  wr_bytes(uc, 0x7C00, boot, sizeof boot);
  wr_bytes(uc, 0x200000, payload, sizeof payload);
  wr16(uc, 0x300000, 0x0010);
  wr16(uc, 0x310000, 0xBEEF);

  set_seg(uc, UC_X86_REG_CS, 0);
  set_seg(uc, UC_X86_REG_DS, 0);

  /* Linear addresses: the payload's HLT at 0x200000+25 is the stop address. */
  CHECK_EQ(uc_emu_start(uc, 0x7C00, 0x200000 + 25, 0, 0), UC_ERR_OK);

  {
    uint16_t cs = 0;
    CHECK_EQ(uc_reg_read(uc, UC_X86_REG_CS, &cs), UC_ERR_OK);
    CHECK_EQ(cs, 0x0008);
  }
  /* Selector arithmetic (+8) reached the next descriptor (data2). */
  CHECK_EQ(rd16(uc, 0x300002), 0xBEEF);
  uc_close(uc);
}

int main(void) {
  test_real_mode_x87_round_to_even();
  test_real_mode_fninit_control_word();
  test_protected_mode_descriptor_above_1mb();
  return TEST_DONE();
}
