/* Decompiled code segment 2 (NE segment 2, selector 0x0010).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg02.c; Ghidra's pseudo-C in re/decomp/seg02 is only a reading aid. */
#include <string.h>

#include "src/stars.h"

/* Segment 2 is the tech-requirement tables (records inside this code segment) and the check "may the
 * current player use item (kind, index)". The record the check works on (a near pointer, DGROUP) is
 * +0 kind word, +2 index word (low byte used), +4/+6 far pointer to the item's table record.
 * Functions that call others mirror their frames (rule d); the near calls inside the segment are
 * PUSH CS; CALL near, i.e. far calls, and reach the callee through rt_call_far. 2:0x587a is a leaf
 * and uses ordinary C locals. The callees in other segments are not decompiled yet: CALL_ORIG.
 * Names: Ghidra's are all generic (FUN_1008_<off>), so the C names describe what each function does;
 * kind<K>_record returns the record of the table that 2:0x50f8 uses for kind bit 0x<K>, and
 * seg02_ptr_<off> returns the constant far pointer cs:<off>. The old fn_02_<off> names are kept as
 * aliases in the comment above each function. */
static inline uint8_t rd8(uint16_t seg, uint16_t off) { return *(uint8_t *)LIN(MK_FP(seg, off)); }
static inline void wr8(uint16_t seg, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(seg, off)) = v; }
static inline uint16_t rd16(uint16_t seg, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 2);
  return v;
}
static inline void wr16(uint16_t seg, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 2); }
/* DGROUP (DS) bytes and words. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }

/* kind400_record: 2:0x0000 (Ghidra FUN_1008_0000, alias fn_02_0000).
 * 15 bytes, no calls.
 *   PUSH BP; MOV BP,SP; IMUL AX,[BP+6],0x8F; ADD AX,0x5C3; MOV DX,CS; LEAVE; RETF
 * Returns the far pointer cs:(index * 0x8f + 0x5c3): the address of record `index` (0x8f bytes
 * each) of a table that starts at 0x5c3 inside this code segment. Ghidra shows only the AX half
 * ("int"); DX = CS is part of the return value and the diff compares it. The 16-bit product wraps,
 * so the index is truncated to 16 bits before the multiply and the sum before it is stored. */
DECOMP(2, 0x0000, kind400_record, FAR_CDECL)
uint32_t kind400_record(int16_t index)
{
  uint16_t off = (uint16_t)((uint16_t)index * 0x8f + 0x5c3);
  return MK_FP(SEG_SEL(2), off);
}

/* kind4000_400_record: 2:0x507c (Ghidra FUN_1008_507c, alias fn_02_507c).
 * 38 bytes. PUSH BP; MOV BP,SP; PUSH SI.
 * FAR CDECL (index, signed). Items 0 to 0x1f are the 0x8f-byte records from cs:0x14ac; from 0x20 up
 * the items continue in the table of 2:0x0000 (a far call, PUSH CS; CALL near), index - 0x20. The
 * compare is JL, a signed one, so a negative index takes the first branch and wraps. Returns DX:AX. */
DECOMP(2, 0x507c, kind4000_400_record, FAR_CDECL)
uint32_t kind4000_400_record(int16_t index)
{
  FRAME_ENTER_REGS(0, 1);
  uint32_t r;
  if (index >= 0x20) {
    uint16_t a[1] = {(uint16_t)(index - 0x20)};
    r = rt_call_far(MK_FP(SEG_SEL(2), 0x0000), FAR_CDECL, a, 1);
  } else {
    r = MK_FP(SEG_SEL(2), (uint16_t)((uint16_t)index * 0x8f + 0x14ac));
  }
  FRAME_LEAVE();
  return r;
}

/* kind1_record: 2:0x50a2 (Ghidra FUN_1008_50a2, alias fn_02_50a2).
 * 14 bytes, no calls. PUSH BP; MOV BP,SP; IMUL AX,[BP+6],0x4E;
 * ADD AX,0x4B9C; MOV DX,CS; LEAVE; RETF. Returns cs:(index * 0x4e + 0x4b9c). Ghidra scales the
 * index by 0x27 because it types the table as words. */
DECOMP(2, 0x50a2, kind1_record, FAR_CDECL)
uint32_t kind1_record(int16_t index)
{
  return MK_FP(SEG_SEL(2), (uint16_t)((uint16_t)index * 0x4e + 0x4b9c));
}

/* kind2_record: 2:0x50b0 (Ghidra FUN_1008_50b0, alias fn_02_50b0).
 * 14 bytes, no calls. Returns cs:(index * 0x38 + 0x4594). */
DECOMP(2, 0x50b0, kind2_record, FAR_CDECL)
uint32_t kind2_record(int16_t index)
{
  return MK_FP(SEG_SEL(2), (uint16_t)((uint16_t)index * 0x38 + 0x4594));
}

/* seg02_ptr_088e: 2:0x50be (Ghidra FUN_1008_50be, alias fn_02_50be).
 * 6 bytes: MOV AX,0x88E; MOV DX,CS; RETF. No argument. Returns
 * cs:0x88e (Ghidra shows only the AX half). */
DECOMP(2, 0x50be, seg02_ptr_088e, FAR_CDECL)
uint32_t seg02_ptr_088e(void)
{
  return MK_FP(SEG_SEL(2), 0x088e);
}

/* seg02_ptr_038f: 2:0x50c4 (Ghidra FUN_1008_50c4, alias fn_02_50c4).
 * 6 bytes: MOV AX,0x38F; MOV DX,CS; RETF. Returns cs:0x38f. */
DECOMP(2, 0x50c4, seg02_ptr_038f, FAR_CDECL)
uint32_t seg02_ptr_038f(void)
{
  return MK_FP(SEG_SEL(2), 0x038f);
}

/* kind8000_record: 2:0x50ca (Ghidra FUN_1008_50ca, alias fn_02_50ca).
 * 14 bytes, no calls. Returns cs:(index * 0x36 + 0x369a). */
DECOMP(2, 0x50ca, kind8000_record, FAR_CDECL)
uint32_t kind8000_record(int16_t index)
{
  return MK_FP(SEG_SEL(2), (uint16_t)((uint16_t)index * 0x36 + 0x369a));
}

/* The check below (2:0x50f8) reads the current player's row of 0xc0 bytes at DS:0x5a64 +
 * 0xc0 * DS:0x14c (a 16-bit product) through three callees:
 *   28:0x21ee(row, 0xe)  the signed byte at row + 0x4c (the "race" value; returned sign-extended)
 *   28:0x2230(row, n)    bit n of the dword at row + 0x4e (a trait), 1 or 0
 *   27:0x4af6(record)    1 when the item (kind, index) has a bit in the word at row + 0x52 that is clear */
static uint16_t row_off(void) { return (uint16_t)(0x5a64 + (uint16_t)(drd16(0x014c) * 0xc0)); }
static uint16_t trait(uint16_t n)
{
  return (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, row_off(), n);
}

/* check_item_usable: 2:0x50f8 (Ghidra FUN_1008_50f8, alias fn_02_50f8).
 * 1267 bytes. ENTER 6,0; PUSH DI; PUSH SI.
 * FAR CDECL (p, a near pointer to the request record: +0 kind, +2 index word, +4/+6 table pointer).
 * "May the current player use item (kind, index)?" Returns AX only: 0 for an unknown kind or an
 * index past the kind's table (the record's pointer is not stored then), 0xffff when a rule forbids
 * the item, otherwise the answer of 2:0x587a (the tech levels) for the item's requirement bytes.
 * The kind is one bit of the word: 0x1, 0x2, 0x4, 0x8, 0x10, 0x20, 0x40, 0x80, 0x100 ... 0x8000
 * (sixteen kinds), each with its own table in this code segment (stride and base per kind below),
 * and each allows the item only under some rules of the player's race value (DI = 28:0x21ee), traits
 * (28:0x2230) and mask (27:0x4af6). The race value is DI in the original and `di` here.
 * Blocks carry the offset of their first instruction. Shared tails: L51a5 returns 0, L51cb returns
 * 0xffff, L582e asks 2:0x587a, L5244 asks 27:0x4af6 (zero: allowed, goes to L582e, else forbidden).
 * BP-2 holds the word [p+2] (only the low byte is used again and again), BP-6 the index. When
 * DS:0x14c is 0xffff (no current player) most kinds skip every rule and ask 2:0x587a at once.
 * Ghidra stores 0x10d8 as the segment of the table pointer; the instruction is MOV [SI+6],CS. */
#define W2 BP_LOCAL(uint16_t, -2)
#define W6 BP_LOCAL(uint16_t, -6)
#define B2 ((uint8_t)W2)
#define NOPLAYER (drd16(0x014c) == 0xffff)
#define SET_PTR(off)                    \
  do {                                  \
    dwr16((uint16_t)(p + 4), (off));    \
    dwr16((uint16_t)(p + 6), SEG_SEL(2)); \
  } while (0)
#define Z(c)                            \
  do {                                  \
    if (c) goto L582e;                  \
    goto L51cb;                         \
  } while (0)
DECOMP(2, 0x50f8, check_item_usable, FAR_CDECL)
uint16_t check_item_usable(uint16_t p)
{
  FRAME_ENTER_REGS(6, 2);
  uint16_t di, kind;
  di = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row_off(), 0x0e);
  kind = drd16(p);
  W2 = drd16((uint16_t)(p + 2));
  if (kind == 0x80) goto L53d4;
  if (kind > 0x80) {
    if (kind == 0x800) goto L55a4;
    if (kind > 0x800) {
      if (kind == 0x1000) goto L5626;
      if (kind == 0x2000) goto L5672;
      if (kind == 0x4000) goto L56b8;
      if (kind == 0x8000) goto L57a8;
      goto L51a5;
    }
    if (kind == 0x100) goto L5462;
    if (kind == 0x200) goto L54e2;
    if (kind == 0x400) goto L554a;
    goto L51a5;
  }
  if (kind == 0x40) goto L538e;
  if (kind > 0x40) goto L51a5;
  {
    uint8_t al = (uint8_t)kind;
    if (al == 0x10) goto L532a;
    if (al > 0x10) {
      if (al == 0x20) goto L5366;
      goto L51a5;
    }
    if (al == 1) goto L5198;
    if (al == 2) goto L5256;
    if (al == 4) goto L52c2;
    if (al == 8) goto L52fc;
    goto L51a5;
  }

L5198: /* kind 0x1: 0x4e-byte records from cs:0x4b9c, 16 of them */
  W6 = B2;
  if (W6 >= 0x10) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x4e + 0x4b9c));
  if (NOPLAYER) goto L582e;
  if (W6 == 0 && di != 0) goto L51cb;
  if ((W6 >= 0xb && W6 <= 0xf) || W6 == 0xa) {
    if (trait(7) != 0) goto L51cb;
  }
  W6 = B2;
  if (W6 == 0xf || W6 == 2) {
    if (trait(0) == 0) goto L51cb;
  }
  if (B2 == 7) {
    if (trait(7) == 0) goto L51cb;
  }
  goto L5244;

L5256: /* kind 0x2: 0x38-byte records from cs:0x4594, 16 */
  W6 = B2;
  if (W6 >= 0x10) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x38 + 0x4594));
  if (NOPLAYER) goto L582e;
  if (W6 == 7 || W6 == 8 || W6 == 0xc) {
    if (trait(0x0a) != 0) goto L51cb;
  }
  W6 = B2;
  if (W6 == 6 || W6 == 5 || W6 == 0xe) {
    di = (uint16_t)(di - 1);
    Z(di == 0);
  }
  goto L582e;

L52c2: /* kind 0x4: 0x36-byte records from cs:0x4378, 10 */
  W6 = B2;
  if (W6 >= 0x0a) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x4378));
  if (W6 == 4 && di != 1) goto L51cb;
  if (W6 == 3 && di != 4) goto L51cb;
  goto L5244;

L52fc: /* kind 0x8: 0x36-byte records from cs:0x4914, 12 */
  W6 = B2;
  if (W6 >= 0x0c) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x4914));
  if (W6 == 7 && di != 1) goto L51cb;
  if (W6 == 6 && di != 4) goto L51cb;
  goto L5244;

L532a: /* kind 0x10: 0x3c-byte records from cs:0x268c, 24 */
  W6 = B2;
  if (W6 >= 0x18) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x3c + 0x268c));
  if (W6 == 2 && di != 4) goto L51cb;
  if ((W6 == 0x10 || W6 == 0xe) && di != 2) goto L51cb;
  goto L5244;

L5366: /* kind 0x20: 0x3c-byte records from cs:0x2c2c, 12 */
  W6 = B2;
  if (W6 >= 0x0c) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x3c + 0x2c2c));
  if (NOPLAYER) goto L582e;
  goto L5244;

L538e: /* kind 0x40: 0x3a-byte records from cs:0x2efc, 15 */
  W6 = B2;
  if (W6 >= 0x0f) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x3a + 0x2efc));
  if (NOPLAYER) goto L582e;
  if (W6 >= 0xa && W6 <= 0xe && di == 4) goto L51cb;
  if (W6 == 9 && di != 3) goto L51cb;
  goto L5244;

L53d4: /* kind 0x80: 0x36-byte records from cs:0x39c4, 8 */
  W6 = B2;
  if (W6 >= 8) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x39c4));
  if (NOPLAYER) goto L582e;
  if (W6 == 2 || W6 == 3 || W6 == 4 || W6 == 0 || W6 == 5) {
    if (trait(9) != 0) goto L51cb;
  }
  W6 = B2;
  if (W6 == 0 || W6 == 5) {
    if (trait(2) == 0) goto L51cb;
  }
  if (B2 == 7 && di != 3) goto L51cb;
  goto L5244;

L5462: /* kind 0x100: 0x36-byte records from cs:0x3b74, 10 */
  W6 = B2;
  if (W6 >= 0x0a) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x3b74));
  if (NOPLAYER) goto L582e;
  if (W6 == 0 || W6 == 2 || W6 == 3 || W6 == 4 || W6 == 5 || W6 == 6 || W6 == 8 || W6 == 9) {
    if (di != 5) goto L51cb;
  }
  if (W6 == 7 && di != 5 && di != 4) goto L51cb;
  if (W6 == 1 && di == 2) goto L51cb;
  goto L582e;

L54e2: /* kind 0x200: 0x38-byte records from cs:0x000f, 16 */
  W6 = B2;
  if (W6 >= 0x10) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x38 + 0x000f));
  if (NOPLAYER) goto L582e;
  if (W6 >= 7 && W6 <= 0xf) {
    if (W6 == 9) goto L582e;
    if (W6 == 0xc) goto L582e;
    Z(di == 6);
  }
  if (W6 > 6) goto L582e;
  if (di != 7) {
    if (W6 == 1) goto L51cb;
    if (W6 >= 4) goto L51cb;
  }
  Z(di != 0);

L554a: /* kind 0x400: 0x8f-byte records from cs:0x05c3, 5 (the table of 2:0x0000) */
  W6 = B2;
  if (W6 >= 5) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x8f + 0x05c3));
  if (NOPLAYER) goto L582e;
  if (W6 == 1 || W6 == 3) {
    if (trait(3) == 0) goto L51cb;
  }
  if (B2 != 4) goto L582e;
  Z(di == 8);

L55a4: /* kind 0x800: 0x36-byte records from cs:0x3fe2, 17 */
  W6 = B2;
  if (W6 >= 0x11) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x3fe2));
  if (NOPLAYER) goto L582e;
  if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(27), 0x4af6), FAR_CDECL, &p, 1) != 0) goto L51cb;
  /* the original jumps through a word table at cs:0x55ec indexed by the index byte */
  switch (B2) {
    case 0:
    case 3:
      di = (uint16_t)(di - 1);
      Z(di == 0);
    case 8:
    case 0xb:
    case 0xf:
      Z(di == 4);
    case 0xd:
      Z(di == 0);
    case 0xe:
      Z(di == 5);
    case 0x10:
      Z(di == 7);
    default:
      goto L582e;
  }

L5626: /* kind 0x1000: 0x36-byte records from cs:0x3d90, 11 */
  W6 = B2;
  if (W6 >= 0x0b) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x3d90));
  if (NOPLAYER) goto L582e;
  if ((uint16_t)rt_call_far(MK_FP(SEG_SEL(27), 0x4af6), FAR_CDECL, &p, 1) != 0) goto L51cb;
  W6 = B2;
  if (W6 == 0 && di == 8) goto L51cb;
  if (W6 != 1) goto L582e;
  Z(di == 8);

L5672: /* kind 0x2000: 0x36-byte records from cs:0x3262, 20 */
  W6 = B2;
  if (W6 >= 0x14) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x3262));
  if (NOPLAYER) goto L582e;
  if (W6 > 7) goto L582e;
  Z(trait(1) != 0);

L56b8: /* kind 0x4000: 0x8f-byte records from cs:0x14ac, 32 (the first table of 2:0x507c) */
  W6 = B2;
  if (W6 >= 0x20) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x8f + 0x14ac));
  if (NOPLAYER) goto L582e;
  if ((W6 == 0xe || W6 == 0x1f) && di != 0) goto L51cb;
  if ((W6 == 0x19 || W6 == 3) && di != 4) goto L51cb;
  if (W6 == 0x16 || W6 == 0x17 || W6 == 0x14 || W6 == 0x18) {
    if (trait(9) != 0) goto L51cb;
  }
  W6 = B2;
  if (W6 == 0x14 || W6 == 0x16 || W6 == 0x18) {
    if (trait(2) == 0) goto L51cb;
  }
  W6 = B2;
  if ((W6 == 10 || W6 == 8) && di != 2) goto L51cb;
  if (W6 == 0xc && di != 1) goto L51cb;
  if (W6 == 0x12 && di != 1) goto L51cb;
  if ((W6 == 0x1b || W6 == 0x1c) && di != 5) goto L51cb;
  goto L5244;

L57a8: /* kind 0x8000: 0x36-byte records from cs:0x369a, 15 (the table of 2:0x50ca) */
  W6 = B2;
  if (W6 >= 0x0f) goto L51a5;
  SET_PTR((uint16_t)(W6 * 0x36 + 0x369a));
  if (NOPLAYER) goto L582e;
  if (W6 <= 8) {
    /* LES BX,[SI+4]: the record's word at +0x34, signed */
    if ((int16_t)rd16(drd16((uint16_t)(p + 6)), (uint16_t)(drd16((uint16_t)(p + 4)) + 0x34)) < 0) {
      if (trait(0x0a) != 0) goto L51cb;
    }
  }
  W6 = B2;
  if (W6 <= 8 && di == 8) goto L51cb;
  if (W6 >= 9 && W6 <= 0xd && di == 8) goto L51cb;
  if (W6 >= 0xb && W6 <= 0xd && di == 2) goto L51cb;
  goto L5244;

L5244: /* 27:0x4af6(p) == 0 allows the item */
  Z((uint16_t)rt_call_far(MK_FP(SEG_SEL(27), 0x4af6), FAR_CDECL, &p, 1) == 0);

L51cb:
  FRAME_LEAVE();
  return 0xffff;

L51a5:
  FRAME_LEAVE();
  return 0;

L582e: /* ask the tech levels: 2:0x587a(far pointer p->table + 2) */
  {
    uint16_t a[2] = {(uint16_t)(drd16((uint16_t)(p + 4)) + 2), drd16((uint16_t)(p + 6))};
    uint16_t r = (uint16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x587a), FAR_CDECL, a, 2);
    FRAME_LEAVE();
    return r;
  }
}

/* set_item_and_check: 2:0x50d8 (Ghidra FUN_1008_50d8, alias fn_02_50d8).
 * 31 bytes. PUSH BP; MOV BP,SP (no locals, no pushes).
 * FAR CDECL (p, kind, index): sets the request record's kind word to `kind`, byte +2 to `index`
 * (only the low byte of the argument word is read) and byte +3 to 0, then answers 2:0x50f8(p).
 * AX is that answer. */
DECOMP(2, 0x50d8, set_item_and_check, FAR_CDECL)
uint16_t set_item_and_check(uint16_t p, uint16_t kind, uint8_t index)
{
  FRAME_ENTER(0);
  /* MOV [BX],AX; MOV AL,index; MOV AH,[BX+3]; MOV [BX+2],AX; MOV BYTE [BX+3],0 */
  dwr16(p, kind);
  dwr16((uint16_t)(p + 2), (uint16_t)((drd8((uint16_t)(p + 3)) << 8) | index));
  wr8(MEM_SEL_DGROUP, (uint16_t)(p + 3), 0);
  uint16_t a[1] = {p};
  uint16_t r = (uint16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, a, 1);
  FRAME_LEAVE();
  return r;
}

/* find_highest_usable_kind8000: 2:0x5842 (Ghidra FUN_1008_5842, alias fn_02_5842).
 * 55 bytes. PUSH BP; MOV BP,SP; PUSH SI.
 * FAR CDECL (p): the request record gets kind 0x8000 and index byte 8 (byte +3 kept), then
 * 2:0x50f8(p) is asked for the index 8 and each lower one until it answers 1 or the index is 0.
 * AX leaves as the last answer minus 1 (DEC AX; the original returns it, Ghidra shows void). */
DECOMP(2, 0x5842, find_highest_usable_kind8000, FAR_CDECL)
uint16_t find_highest_usable_kind8000(uint16_t p)
{
  FRAME_ENTER_REGS(0, 1);
  uint16_t r;
  /* MOV AX,[SI+2]; AND AL,8; OR AL,8: the low byte becomes 8 */
  dwr16((uint16_t)(p + 2), (uint16_t)((drd16((uint16_t)(p + 2)) & 0xff00) | 8));
  dwr16(p, 0x8000);
  for (;;) {
    uint16_t a[1] = {p};
    r = (uint16_t)((uint16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, a, 1) - 1);
    if (r == 0) break;
    if (drd8((uint16_t)(p + 2)) == 0) break;
    /* MOV AL,[SI+2]; DEC AL; MOV AH,[SI+3]; MOV [SI+2],AX */
    dwr16((uint16_t)(p + 2), (uint16_t)((drd16((uint16_t)(p + 2)) & 0xff00) | (uint8_t)(drd8((uint16_t)(p + 2)) - 1)));
  }
  FRAME_LEAVE();
  return r;
}

/* check_tech_requirements: 2:0x587a (Ghidra FUN_1008_587a, alias fn_02_587a).
 * 187 bytes. ENTER 0xa,0; PUSH DI; PUSH SI. A leaf: ordinary C locals.
 * FAR CDECL (item, a far pointer to six signed requirement bytes): compares them with the current
 * player's six tech levels (the signed bytes at DS:0x5a7e + 0xc0 * DS:0x14c, 16-bit product). A level
 * below the requirement counts as a missing one; a missing level whose number is the low nibble of
 * the byte DS:0x5a9d (+ row) either is exactly one short (flag) or sets `field` = number + 1.
 * Result: 1 when nothing is missing; 2 when exactly one is missing and it is the focus field and
 * exactly one short; requirement[field - 1] - level[field - 1] + 1 when exactly one is missing and
 * `field` is set (field - 1 is the number of the focus field; the level is read at
 * DS:0x5a7d + row + field); else 99. AX only. */
DECOMP(2, 0x587a, check_tech_requirements, FAR_CDECL)
uint16_t check_tech_requirements(uint32_t item)
{
  uint16_t seg = (uint16_t)(item >> 16), off = (uint16_t)item;
  uint16_t row = (uint16_t)(drd16(0x014c) * 0xc0);
  uint16_t missing = 0, flag = 0, field = 0, si;
  for (si = 0; si < 6; si++) {
    int8_t cur = (int8_t)drd8((uint16_t)(row + si + 0x5a7e));
    int8_t req = (int8_t)rd8(seg, (uint16_t)(off + si));
    if (cur < req) {
      missing++;
      if ((drd8((uint16_t)(row + 0x5a9d)) & 0xf) == si) {
        if ((uint16_t)((int16_t)cur - (int16_t)req + 1) == 0)
          flag = 1;
        else
          field = (uint16_t)(si + 1);
      }
    }
  }
  if (missing == 0) return 1;
  if (missing == 1 && flag != 0) return 2;
  if (missing == 1 && field != 0) {
    int16_t lv = (int8_t)drd8((uint16_t)(row + field + 0x5a7d));
    int16_t rq = (int8_t)rd8(seg, (uint16_t)(field + off - 1));
    return (uint16_t)(rq - lv + 1);
  }
  return 99;
}
