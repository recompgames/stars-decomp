/* Decompiled code segment 1 (NE segment 1, selector 0x0008).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg01.c; Ghidra's pseudo-C in re/decomp/seg01 is only a reading aid (it gets
 * several widths, flag masks and loop bounds of 1:0x0d90 wrong; the disassembly was followed).
 *
 * Segment 1 is the game's start-up and its configuration reader:
 *   1:0x0000  start-up: screen class, regions, colours, pens, brushes, cursors, bitmaps, icons,
 *             resource holders, global blocks, procedure instances of the callbacks
 *   1:0x08a4  build the fonts and measure the text cells
 *   1:0x0af8  create the main window and load the two accelerator tables
 *   1:0x0bb4  lay out the two tables of tool buttons (6 and 7 entries)
 *   1:0x0c36  read one "x,y,w,h" window placement from the ini file
 *   1:0x0d90  read the whole ini file into DGROUP
 *   1:0x1da0  apply a letter string ("ABc*") to a table of 16-byte entries
 * The functions with callees that take or depend on a stack address (1:0x0000, 0x0af8, 0x0c36, 0x08a4, 0x0d90)
 * mirror their frames (rule d): FRAME_ENTER(n + 2 * pushes), the callees that mirror a frame are reached
 * through rt_call_far (this includes 4:0x1c78, decompiled), 8:* and 34:* through CALL_ORIG. 1:0x0bb4 and 1:0x1da0 are leaves with ordinary
 * C locals. DGROUP words are read and written at the original offsets with the helpers below, as in
 * src/seg13.c (the generated globals.h guesses some widths). DS = SS = DGROUP in every function. */
#include <string.h>

#include "src/stars.h"

static inline uint8_t rd8(uint16_t seg, uint16_t off) { return *(uint8_t *)LIN(MK_FP(seg, off)); }
static inline void wr8(uint16_t seg, uint16_t off, uint8_t v) { *(uint8_t *)LIN(MK_FP(seg, off)) = v; }
static inline uint16_t rd16(uint16_t seg, uint16_t off) {
  uint16_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 2);
  return v;
}
static inline void wr16(uint16_t seg, uint16_t off, uint16_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 2); }
static inline uint32_t rd32(uint16_t seg, uint16_t off) {
  uint32_t v;
  memcpy(&v, LIN(MK_FP(seg, off)), 4);
  return v;
}
static inline void wr32(uint16_t seg, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 4); }
/* DGROUP (DS) bytes, words and dwords. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }
static inline void dwr32(uint16_t off, uint32_t v) { wr32(MEM_SEL_DGROUP, off, v); }
/* A pointer to a DGROUP offset, as the far pointer the APIs take. */
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }

/* Copy one 16-byte table entry (REP MOVSD, four dwords) inside DGROUP. */
static void copy_entry(uint16_t dst, uint16_t src)
{
  for (uint16_t i = 0; i < 16; i += 4) dwr32((uint16_t)(dst + i), drd32((uint16_t)(src + i)));
}

/* 1:0x1da0 (Ghidra FUN_1000_1da0), 354 bytes. ENTER 0x20,0; PUSH DI; PUSH SI. A leaf that never
 * exposes a stack address, so ordinary C locals stand for the frame (the 16-byte swap buffer at BP-0x1e
 * is a local array).
 * FAR CDECL: str ([BP+6]) a near pointer to a NUL-terminated string, tab ([BP+8]) a near pointer to a
 * table of n ([BP+10], signed) 16-byte entries, no result. Byte 10 of an entry holds a 3-bit value
 * (bits 0-2), a 4-bit letter index (bits 3-6) and a case flag (bit 7). Each letter of the string
 * (index = letter - 'A' or - 'a', chosen with bit 0 of the character-class table at DS:0x1955; a byte
 * with neither bit 0 nor bit 1 set is skipped, bytes above 0x7f index the table backwards) looks for
 * its entry from position k (the number of letters placed so far) on and moves it to position k,
 * setting its value to the number of stars seen before (at most 1) and its case flag. A letter whose
 * entry is not found is ignored. "*" counts stars (at most 1). At the end the entries from k on get the
 * star count as their value if it is smaller (unsigned). */
DECOMP(1, 0x1da0, fn_01_1da0, FAR_CDECL)
void fn_01_1da0(uint16_t str, uint16_t tab, int16_t n)
{
  uint16_t star = 0;   /* BP-8 */
  uint16_t k = 0;      /* BP-0xa */
  uint16_t cursor;     /* BP-0xe: tab + 16 * k */
  uint16_t p = str;
  if (drd8(p) != 0) {
    cursor = tab;
    for (;;) {
      uint8_t ch = drd8(p);
      if (ch == 0x2a) {
        if (star < 1) star++;
      } else {
        int16_t cx = (int16_t)(int8_t)ch;                       /* CWDE */
        uint8_t cls = drd8((uint16_t)(0x1955 + cx));
        if ((cls & 3) != 0) {
          uint16_t upper = cls & 1;
          int16_t letter = (int16_t)(upper ? cx - 0x41 : cx - 0x61);
          if (letter >= 0) {
            int16_t found = (int16_t)k;
            if ((int16_t)k < n) {
              uint16_t fld = (uint16_t)(k * 16 + tab + 10);
              for (found = (int16_t)k;;) {
                if ((uint16_t)((drd8(fld) & 0x78) >> 3) == (uint16_t)letter) break;
                fld = (uint16_t)(fld + 16);
                found++;
                if (!(found < n)) break;
              }
            }
            if ((uint16_t)n != (uint16_t)found) {
              uint16_t ent = (uint16_t)(((uint16_t)found << 4) + tab);
              uint16_t f = (uint16_t)(ent + 10);
              dwr8(f, (uint8_t)(drd8(f) ^ ((drd8(f) ^ star) & 7)));
              dwr8(f, (uint8_t)(drd8(f) ^ ((drd8(f) ^ (upper << 7)) & 0x80)));
              if (k != (uint16_t)found) {
                uint32_t tmp[4];                                 /* BP-0x1e */
                for (uint16_t i = 0; i < 4; i++) tmp[i] = drd32((uint16_t)(ent + 4 * i));
                copy_entry(ent, cursor);
                for (uint16_t i = 0; i < 4; i++) dwr32((uint16_t)(cursor + 4 * i), tmp[i]);
              }
              cursor = (uint16_t)(cursor + 16);
              k++;
            }
          }
        }
      }
      p = (uint16_t)(p + 1);
      if (drd8(p) == 0) break;
    }
  }
  if ((int16_t)k < n) {
    uint16_t fld = (uint16_t)((k << 4) + tab + 10);
    uint16_t count = (uint16_t)(n - k);
    do {
      if ((uint16_t)(drd8(fld) & 7) < star) dwr8(fld, (uint8_t)(drd8(fld) ^ ((drd8(fld) ^ star) & 7)));
      fld = (uint16_t)(fld + 16);
    } while (--count != 0);
  }
}

/* 1:0x0bb4 (Ghidra FUN_1000_0bb4), 129 bytes. ENTER 0x10,0; PUSH DI. A leaf (no stack address is
 * exposed), so ordinary C locals stand for the frame. FAR CDECL, no arguments, no result.
 * Lays out two tables of 16-byte button entries, 6 at DS:0x07c2 and 7 at DS:0x08d6. Per entry: the word
 * at +0 is the running position (reset to 4 whenever the class, bits 0-2 of the byte at +10, differs
 * from the previous entry's; the run before the first entry has class 0), the word at +2
 * gains the old +0 word times the cell width DS:0x267a (low 16 bits of the signed product), +0 takes the
 * position, bits 3 and 4 of the byte at +11 are cleared, and the position advances by the new +2 word
 * plus 4 when bit 7 of the byte at +10 is set, else by the cell width plus 7. */
DECOMP(1, 0x0bb4, fn_01_0bb4, FAR_CDECL)
void fn_01_0bb4(void)
{
  uint16_t table = 0x07c2;   /* BX */
  int16_t count = 6;         /* DX */
  for (int16_t pass = 0; pass < 2; pass++) {
    uint16_t prev_class = 0;  /* BP-4 */
    uint16_t pos = 4;         /* BP-0xe */
    uint16_t e = table;
    if (count > 0) {
      uint16_t left = (uint16_t)count;
      do {
        uint16_t cls = drd8((uint16_t)(e + 10)) & 7;
        if (cls != prev_class) {
          pos = 4;
          prev_class = drd8((uint16_t)(e + 10)) & 7;
        }
        uint16_t cell = drd16(0x267a);
        dwr16((uint16_t)(e + 2), (uint16_t)(drd16((uint16_t)(e + 2)) + (uint16_t)((uint32_t)drd16(e) * cell)));
        dwr16(e, pos);
        dwr8((uint16_t)(e + 11), (uint8_t)(drd8((uint16_t)(e + 11)) & 0xe7));
        uint16_t adv;
        if ((drd8((uint16_t)(e + 10)) & 0x80) != 0) adv = (uint16_t)(drd16((uint16_t)(e + 2)) + 4);
        else adv = (uint16_t)(drd16(0x267a) + 7);
        pos = (uint16_t)(pos + adv);
        e = (uint16_t)(e + 16);
      } while (--left != 0);
    }
    table = 0x08d6;
    count = 7;
  }
}

/* 1:0x0c36 (Ghidra FUN_1000_0c36), 345 bytes. ENTER 0x28,0; PUSH DI; PUSH SI. FAR CDECL, no result.
 * Reads one window placement from the ini file: app ([BP+6], near pointer to the section name), file
 * ([BP+8], near pointer to the file name), id ([BP+0xa], string-table id of the key name, loaded by
 * 8:0x8dc0 into the buffer at BP-0x28 whose address goes to GetPrivateProfileString, so the frame is
 * mirrored), dest ([BP+0xc], near pointer to eight bytes of four int16 values and one flag byte).
 * GetPrivateProfileString(app, key, DS:0x075a, DS:0x5866, 20, file) must return 17 characters: 'M',
 * 'R' or 'I', then four numbers of 4 characters each: a '-' anywhere in a number negates it and is
 * otherwise skipped, any other character outside '0'..'9' (signed byte compares) rejects the whole
 * value. Accepted: the four values are stored and flag bits 0 ("M"), 1 ("I") and 2 (parsed) are set in
 * the byte at dest+8. Rejected: the values are {0x8000, 0, 0x8000, 0}, bit 0 is set only for the key
 * id 0x94, bits 1 and 2 are cleared. Ghidra's pseudo-C hides that BP-2/-4/-6 hold the three flags. */
DECOMP(1, 0x0c36, fn_01_0c36, FAR_CDECL)
void fn_01_0c36(uint16_t app, uint16_t file, uint16_t id, uint16_t dest)
{
  FRAME_ENTER(0x28 + 4);
  const uint16_t keyp = (uint16_t)(rt_bp - 0x28);
  int16_t val[4];            /* BP-0x10 .. BP-0xa */
  int16_t res[4];            /* BP-0x18 .. BP-0x12 */
  uint16_t is_m, is_i, ok;   /* BP-2, BP-4, BP-6 */
  bool good = false;
  (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, keyp);
  uint16_t n = GetPrivateProfileString(dsfp(app), MK_FP(rt_ss, keyp), dsfp(0x075a), dsfp(0x5866), 0x14, dsfp(file));
  uint8_t c0 = drd8(0x5866);
  if (n == 0x11 && (c0 == 0x4d || c0 == 0x52 || c0 == 0x49)) {
    uint16_t p = 0x5867;
    good = true;
    for (int16_t j = 0; j < 4 && good; j++) {
      uint16_t value = 0;
      bool neg = false;
      for (int16_t cx = 0; cx < 4; cx++) {
        int8_t ch = (int8_t)drd8(p);
        if (ch == 0x2d) {
          neg = true;
        } else {
          if (ch < 0x30 || ch > 0x39) { good = false; break; }
          value = (uint16_t)(value * 10u + (uint16_t)(int16_t)ch - 0x30u);
        }
        p = (uint16_t)(p + 1);
      }
      if (!good) break;
      val[j] = (int16_t)(neg ? (uint16_t)(0u - value) : value);
    }
  }
  if (good) {
    for (int16_t j = 0; j < 4; j++) res[j] = val[j];
    is_m = (c0 == 0x4d);
    is_i = (c0 == 0x49);
    ok = 1;
  } else {
    res[0] = (int16_t)0x8000;
    res[1] = 0;
    res[2] = (int16_t)0x8000;
    res[3] = 0;
    is_m = (id == 0x94);
    is_i = 0;
    ok = 0;
  }
  for (uint16_t j = 0; j < 4; j++) dwr16((uint16_t)(dest + 2 * j), (uint16_t)res[j]);
  uint8_t fl = drd8((uint16_t)(dest + 8));
  fl = (uint8_t)(fl ^ ((fl ^ is_m) & 1));
  fl = (uint8_t)(fl ^ ((fl ^ (is_i + is_i)) & 2));
  fl = (uint8_t)(fl ^ ((fl ^ (ok << 2)) & 4));
  dwr8((uint16_t)(dest + 8), fl);
  FRAME_LEAVE();
}

/* Copy the NUL-terminated string at DS:src to DS:dst, terminator included (SCASB length scan, then
 * REP MOVSW / REP MOVSB). */
static void copy_cstr(uint16_t dst, uint16_t src)
{
  uint16_t len = 0;
  while (drd8((uint16_t)(src + len)) != 0) len++;
  for (uint16_t i = 0; i <= len; i++) dwr8((uint16_t)(dst + i), drd8((uint16_t)(src + i)));
}

/* Height plus external leading of the font selected into hdc: GetTextMetrics into the TEXTMETRIC at
 * BP-0x26 (words at BP-0x26 and BP-0x1e). */
static uint16_t cell_height(uint16_t hdc)
{
  const uint16_t tm = (uint16_t)(rt_bp - 0x26);
  GetTextMetrics(hdc, MK_FP(rt_ss, tm));
  return (uint16_t)(rd16(rt_ss, (uint16_t)(rt_bp - 0x1e)) + rd16(rt_ss, tm));
}

/* -MulDiv(points, pixels per inch of the device, 72), the LOGFONT height of a point size. */
static uint16_t font_height(uint16_t hdc, uint16_t points)
{
  int16_t ppi = GetDeviceCaps(hdc, 0x5a);
  return (uint16_t)(0u - (uint16_t)MulDiv((int16_t)points, ppi, 0x48));
}

/* 1:0x08a4 (Ghidra FUN_1000_08a4), 596 bytes. ENTER 0x26,0; PUSH DI; PUSH SI. FAR CDECL: hdc
 * ([BP+6]); AX = 1. The TEXTMETRIC at BP-0x26 is passed to GetTextMetrics, so the frame is mirrored.
 * Builds the program's fonts from a 50-byte LOGFONT (LocalAlloc 0x40 = LMEM_ZEROINIT, freed at the
 * end): face names that are empty in the table at DS:0x06ca (4 entries of 32 bytes) are loaded from
 * the string table by 8:0x8dc0 (ids 0x51b + index); then
 *   DS:0x4a5a, 0x4a5c  names 0 and 1, height -MulDiv(10, LOGPIXELSY, 72)
 *   DS:0x293a          name 0 (the table copy), 6 pt;   DS:0x2938  7 pt (the name is still name 0)
 *   DS:0x292e .. 0x2934  the four names, 8 pt
 *   DS:0x2936          name 1, 8 pt, lfEscapement 0xc4e
 * and with each of the fonts 0x292e, 0x2938, 0x293a, 0x4a5a selected in turn the cell sizes
 * DS:0x267a, 0x2676, 0x2678, 0x54ee (tmHeight + tmExternalLeading) are stored; the width of the 8
 * characters at DS:0x74a with font 0x292e goes to DS:0x288a. The font selected before is restored.
 * The LOGFONT is the one LocalAlloc block, whose face name (+0x12) is rewritten before each call. */
DECOMP(1, 0x08a4, fn_01_08a4, FAR_CDECL)
uint16_t fn_01_08a4(uint16_t hdc)
{
  FRAME_ENTER(0x26 + 4);
  const uint16_t lf = LocalAlloc(0x40, 0x32);   /* BP-6 */
  uint16_t face;
  for (uint16_t i = 0, name = 0x06ca; name < 0x074a; i++, name = (uint16_t)(name + 0x20)) {
    if (drd8(name) == 0) (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(i + 0x51b), name);
  }
  dwr16(lf, font_height(hdc, 10));
  /* the two 10-point fonts: names 0 and 1 */
  {
    uint16_t slot = 0x4a5a;
    for (face = 0x06ca; face < 0x070a; face = (uint16_t)(face + 0x20)) {
      copy_cstr((uint16_t)(lf + 0x12), face);
      dwr16(slot, CreateFontIndirect(dsfp(lf)));
      slot = (uint16_t)(slot + 2);
    }
  }
  copy_cstr((uint16_t)(lf + 0x12), 0x06ca);
  dwr16(lf, font_height(hdc, 6));
  dwr16(0x293a, CreateFontIndirect(dsfp(lf)));
  dwr16(lf, font_height(hdc, 7));
  dwr16(0x2938, CreateFontIndirect(dsfp(lf)));
  dwr16(lf, font_height(hdc, 8));
  {
    uint16_t slot = 0x292e;
    for (face = 0x06ca; face < 0x074a; face = (uint16_t)(face + 0x20)) {
      copy_cstr((uint16_t)(lf + 0x12), face);
      dwr16(slot, CreateFontIndirect(dsfp(lf)));
      slot = (uint16_t)(slot + 2);
    }
  }
  copy_cstr((uint16_t)(lf + 0x12), 0x06ea);
  dwr16((uint16_t)(lf + 4), 0x0c4e);
  dwr16(0x2936, CreateFontIndirect(dsfp(lf)));
  const uint16_t old = SelectObject(hdc, drd16(0x292e));
  dwr16(0x267a, cell_height(hdc));
  dwr16(0x288a, (uint16_t)GetTextExtent(hdc, dsfp(0x074a), 8));
  SelectObject(hdc, drd16(0x2938));
  dwr16(0x2676, cell_height(hdc));
  SelectObject(hdc, drd16(0x293a));
  dwr16(0x2678, cell_height(hdc));
  SelectObject(hdc, drd16(0x4a5a));
  dwr16(0x54ee, cell_height(hdc));
  SelectObject(hdc, old);
  LocalFree(lf);
  FRAME_LEAVE();
  return 1;
}

/* ---- 1:0x0d90 helpers. The ini file name, the section name and the key name live in the frame of
 * 1:0x0d90 (BP-0x44, BP-0x34, BP-0x24, 16 bytes each); the helpers are called only from that function,
 * so rt_bp is its frame pointer. DS = SS = DGROUP. ---- */
static uint16_t ini_file(void) { return (uint16_t)(rt_bp - 0x44); }
static uint16_t ini_app(void) { return (uint16_t)(rt_bp - 0x34); }
static uint16_t ini_key(void) { return (uint16_t)(rt_bp - 0x24); }

/* 8:0x8dc0 (CDECL id, buffer): copy string-table string id into the DGROUP buffer. */
static void load_string(uint16_t buf, uint16_t id)
{
  (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, buf);
}

/* Load the key name for string id, then GetPrivateProfileInt(section, key, def, file). */
static uint16_t ini_int(uint16_t id, int16_t def)
{
  load_string(ini_key(), id);
  return GetPrivateProfileInt(MK_FP(rt_ss, ini_app()), MK_FP(rt_ss, ini_key()), def, MK_FP(rt_ss, ini_file()));
}

/* Load the key name for string id, then GetPrivateProfileString(section, key, DS:def, DS:dst, size, file). */
static uint16_t ini_str(uint16_t id, uint16_t def, uint16_t dst, uint16_t size)
{
  load_string(ini_key(), id);
  return GetPrivateProfileString(MK_FP(rt_ss, ini_app()), MK_FP(rt_ss, ini_key()), dsfp(def), dsfp(dst), size,
                                 MK_FP(rt_ss, ini_file()));
}

/* Length of the NUL-terminated string at seg:off (SCASB scan). */
static uint16_t str_len(uint16_t seg, uint16_t off)
{
  uint16_t n = 0;
  while (rd8(seg, (uint16_t)(off + n)) != 0) n++;
  return n;
}

/* 1:0x0c36 through the frame-mirroring call: read the placement of key string id into DS:dest. */
static void read_placement(uint16_t id, uint16_t dest)
{
  const uint16_t args[4] = {ini_app(), ini_file(), id, dest};
  (void)rt_call_far(MK_FP(SEG_SEL(1), 0x0c36), FAR_CDECL, args, 4);
}

/* A placement read into the 8-byte buffer at BP-0x14 becomes a rectangle record in DGROUP: left and
 * top (a dword copy), then width and height as right - left and bottom - top; a left of 0x8000 means
 * "not in the file" and leaves the record alone. */
static void load_rect(uint16_t id, uint16_t dst)
{
  const uint16_t plc = (uint16_t)(rt_bp - 0x14);
  read_placement(id, plc);
  if (drd16(plc) != 0x8000) {
    dwr32(dst, drd32(plc));
    dwr16((uint16_t)(dst + 4), (uint16_t)(drd16((uint16_t)(plc + 4)) - drd16(plc)));
    dwr16((uint16_t)(dst + 6), (uint16_t)(drd16((uint16_t)(plc + 6)) - drd16((uint16_t)(plc + 2))));
  }
}

/* A window size read from the ini file, kept in 10..2000. */
static uint16_t clamp_size(uint16_t v)
{
  if ((int16_t)v < 10) v = 10;
  if ((int16_t)v > 2000) v = 2000;
  return v;
}

/* A report-sort setting: bit 8 is a flag and the low byte the column; stores (flag, column). */
static void load_sort(uint16_t id, uint16_t flag_off, uint16_t col_off)
{
  uint16_t v = ini_int(id, 0);
  dwr16(flag_off, (uint16_t)((v & 0x100) != 0));
  dwr16(col_off, (uint16_t)(v & 0xff));
}

/* A report-field setting (default -1): the dword at off gets the value zero-extended. */
static void load_fields(uint16_t id, uint16_t off)
{
  dwr16(off, ini_int(id, -1));
  dwr16((uint16_t)(off + 2), 0);
}

/* DGROUP word at off: bit `bit` := (value != 0), the XOR-mask way the original writes it. */
static void set_flag_bit(uint16_t off, uint16_t bit, uint16_t value)
{
  uint16_t mask = (uint16_t)(1u << bit);
  uint16_t v = (uint16_t)((value != 0) << bit);
  dwr16(off, (uint16_t)(drd16(off) ^ ((v ^ drd16(off)) & mask)));
}

/* 1:0x0d90 (Ghidra FUN_1000_0d90), 4112 bytes. ENTER 0x46,0; PUSH DI; PUSH SI. FAR CDECL, no result.
 * Reads every setting of "Stars.ini" (C:\WINDOWS) into DGROUP. The section names, the key names and the
 * file name are string-table strings (8:0x8dc0) in buffers of the frame (BP-0x34, BP-0x24, BP-0x44),
 * whose addresses go to GetPrivateProfileInt / String, so the frame is mirrored. Sections: Windows
 * (window placements through 1:0x0c36, the resolution warning, layout and window sizes, toolbar,
 * tile letters through 1:0x1da0), Files, Misc, Fonts, ZipOrders. The settings and their globals are
 * listed in order in the body. The frame locals BP-2 .. BP-0xa and BP-0x46 are ordinary C locals. */
DECOMP(1, 0x0d90, fn_01_0d90, FAR_CDECL)
void fn_01_0d90(void)
{
  FRAME_ENTER(0x46 + 4);
  const uint16_t app = ini_app(), key = ini_key();
  uint16_t v, n, i, len, ax;

  dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xe3));
  load_string(app, 0x90);
  load_string(ini_file(), 0x91);
  read_placement(0x94, 0x2510);
  load_rect(0xb2, 0x1694);
  load_rect(0xb3, 0x16ca);
  load_rect(0xb4, 0x1700);
  load_rect(0xb1, 0x165e);
  /* "Resolution": a warning box unless it is set, or the display is 800x600 or larger */
  if (ini_int(0xad, 0) == 0 && ((int16_t)drd16(0x54f6) <= 4 || (drd8(0x0791) & 0xc0) == 0))
    (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x02b9, 0x0010);
  /* "Layout" 0..2 */
  v = ini_int(0xa3, 1);
  dwr16(0x255e, v);
  if ((int16_t)v < 0) v = 0;
  if ((int16_t)v > 2) v = 2;
  dwr16(0x255e, v);
  /* "Style1Width", "Style1Height", "Style1Height2", "Style2Width", "Style2Height", "Style2Height2" */
  v = ini_int(0xa5, 0x18c); dwr16(0x2654, v); dwr16(0x2654, clamp_size(v));
  v = ini_int(0xa6, 0x6e);  dwr16(0x2656, v); dwr16(0x2656, clamp_size(v));
  v = ini_int(0xa7, 0xc0);  dwr16(0x2658, v); dwr16(0x2658, clamp_size(v));
  v = ini_int(0xa8, 0x18c); dwr16(0x265a, v); dwr16(0x265a, clamp_size(v));
  v = ini_int(0xa9, 0x6e);  dwr16(0x265c, v); dwr16(0x265c, clamp_size(v));
  v = ini_int(0xaa, 0xc0);  dwr16(0x265e, v); dwr16(0x265e, clamp_size(v));
  /* "Toolbar": bit 15 of DS:0x0792 */
  v = ini_int(0xab, 1);
  {
    uint16_t cx = (uint16_t)((v != 0) << 15);
    uint16_t t = (uint16_t)((cx ^ drd16(0x0792)) & 0x7fff);
    dwr16(0x0792, (uint16_t)(cx ^ t));
  }
  /* "GlobalSettings": a 28-character value goes through 4:0x1c78 into DS:0x0872, else that dword is 0 */
  if (ini_str(0xa4, 0x075c, 0x5866, 0x28) == 0x1c) {
    const uint16_t a[3] = {0x0872, 0x5382, 0x5866};
    (void)rt_call_far(MK_FP(SEG_SEL(4), 0x1c78), FAR_CDECL, a, 3);
  } else {
    dwr32(0x0872, 0);
  }
  /* "PlanetTiles" and "ShipTiles": letter strings applied to the two button tables */
  (void)ini_str(0x96, 0x075e, 0x5866, 0x14);
  fn_01_1da0(0x5866, 0x07c2, 6);
  (void)ini_str(0x95, 0x0760, 0x5866, 0x14);
  fn_01_1da0(0x5866, 0x08d6, 7);
  /* "Selection": a letter S, E or P (bits 5 to 8 of DS:0x251a), a letter B..Q (DS:0x2520) and a number */
  n = ini_str(0x98, 0x0762, 0x5866, 0x14);
  if ((int16_t)n < 3) {
    dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
  } else {
    uint16_t c = (uint16_t)(int16_t)(int8_t)drd8(0x5866);
    uint16_t w = drd16(0x251a);
    if (c == 0x53) w = (uint16_t)((w & 0xfe5f) | 0x40);
    else if (c > 0x53) w = (uint16_t)(w & 0xfe1f);
    else if (c == 0x45) w = (uint16_t)((w & 0xfe9f) | 0x80);
    else if (c == 0x50) w = (uint16_t)((w & 0xfe3f) | 0x20);
    else w = (uint16_t)(w & 0xfe1f);
    dwr16(0x251a, w);
    if ((int8_t)drd8(0x5867) < 0x42 || (int8_t)drd8(0x5867) > 0x51) {
      dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1f));
    } else {
      dwr16(0x2520, (uint16_t)((int16_t)(int8_t)drd8(0x5867) - 0x42));
    }
    if ((drd16(0x251a) & 0x01e0) != 0) dwr16(0x251e, (uint16_t)CALL_ORIG(34, 0x037a, FAR_CDECL, 1, 0x5868));
  }
  dwr16(0x2528, ini_int(0xc8, 0));
  /* "GameID": up to 10 hexadecimal digits into the dword DS:0x2522 (other characters count as 0) */
  n = ini_str(0xac, 0x0764, 0x5866, 0x0a);
  dwr32(0x2522, 0);
  if ((int16_t)n > 0) {
    for (i = 0; (int16_t)i < (int16_t)n; i++) {
      int8_t c = (int8_t)drd8((uint16_t)(0x5866 + i));
      uint32_t id = drd32(0x2522) << 4;
      dwr32(0x2522, id);
      if (c >= 0x30 && c <= 0x39) ax = (uint16_t)((int16_t)c - 0x30);
      else if (c >= 0x61 && c <= 0x66) ax = (uint16_t)((int16_t)c - 0x57);
      else continue;
      dwr32(0x2522, id + ax);
    }
  }
  v = ini_int(0x97, 4);
  if (v >= 1 && v <= 9) dwr16(0x0576, (uint16_t)(v - 5));
  dwr16(0x54ec, ini_int(0x9e, 0));
  dwr16(0x538e, ini_int(0x9f, 0));
  dwr16(0x4c76, (uint16_t)(ini_int(0xa0, 0xf) & 0xf));
  v = ini_int(0xa1, 0x64);
  if (v > 0x64) v = 0x64;
  dwr16(0x0560, v);
  v = ini_int(0x9d, 0xe0);
  ax = (uint16_t)(v & 0xc00f);
  dwr16(0x055e, ax);
  if (ax >= 6) {
    dwr16(0x055e, 0);
    dwr16(0x54ec, 0);
  } else {
    dwr16(0x055e, (uint16_t)(drd16(0x055e) | (v & 0x3ff0)));
  }
  v = ini_int(0xa2, (int16_t)drd16(0x04c8));
  dwr16(0x04c8, v);
  if ((int16_t)v < 100 || (int16_t)v > 30000) dwr16(0x04c8, 0x1388);
  /* section "Files": "Wait" is bit 2 of DS:0x251a, "File1" the game file (bit 0, copied to DS:0x5764) */
  load_string(app, 0x99);
  v = ini_int(0x9a, 0);
  dwr16(0x251a, (uint16_t)(drd16(0x251a) ^ ((((uint8_t)v << 2) ^ drd8(0x251a)) & 4)));
  n = ini_str(0x9b, 0x0766, 0x5866, 0x100);
  if ((int16_t)n > 3) {
    dwr8(0x251a, (uint8_t)(drd8(0x251a) | 1));
    copy_cstr(0x5764, 0x5866);
  } else {
    dwr8(0x251a, (uint8_t)(drd8(0x251a) & 0xfe));
  }
  if (drd32(0x05f6) == 0) {
    const uint16_t aargs[2] = {0x0900, 0x0009};
    uint32_t blk = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, aargs, 2);
    dwr16(0x05f6, (uint16_t)blk);
    dwr16(0x05f8, (uint16_t)(blk >> 16));
  }
  /* "File1".."File9": up to nine 256-byte entries at the far block DS:0x05f6, then packed to the front */
  {
    const uint16_t keylen = str_len(MEM_SEL_DGROUP, key);
    const uint16_t last = (uint16_t)(key + keylen - 1);
    uint16_t blk_off, blk_seg, si;
    for (i = 0, si = 0; (int16_t)si < 0x900; i++, si = (uint16_t)(si + 0x100)) {
      dwr8(last, (uint8_t)(i + 0x31));
      blk_off = drd16(0x05f6);
      blk_seg = drd16(0x05f8);
      n = GetPrivateProfileString(MK_FP(rt_ss, app), MK_FP(rt_ss, key), dsfp(0x0768),
                                  MK_FP(blk_seg, (uint16_t)(blk_off + si)), 0x100, MK_FP(rt_ss, ini_file()));
      if ((int16_t)n < 4) wr8(blk_seg, (uint16_t)(blk_off + si), 0);
    }
    {
      uint16_t placed = 0, idx = 0, dst = 0, src = 0;
      for (; (int16_t)src < 0x900; idx++, src = (uint16_t)(src + 0x100)) {
        blk_off = drd16(0x05f6);
        blk_seg = drd16(0x05f8);
        if (rd8(blk_seg, (uint16_t)(blk_off + src)) != 0) {
          if (placed != idx) {
            uint16_t l = str_len(blk_seg, (uint16_t)(blk_off + src));
            for (uint16_t k = 0; k <= l; k++)
              wr8(blk_seg, (uint16_t)(blk_off + dst + k), rd8(blk_seg, (uint16_t)(blk_off + src + k)));
            wr8(blk_seg, (uint16_t)(drd16(0x05f6) + src), 0);
          }
          dst = (uint16_t)(dst + 0x100);
          placed++;
        }
      }
    }
  }
  dwr16(0x251c, ini_int(0x9c, (int16_t)drd16(0x0082)));
  /* section "Misc" */
  load_string(app, 0x92);
  (void)ini_str(0xae, 0x076a, 0x27c2, 0x10);
  set_flag_bit(0x0792, 10, ini_int(0xaf, 0));
  v = ini_int(0xb0, 1);
  dwr16(0x09aa, v);
  if ((int16_t)v < 1 || (int16_t)v > 0x3e7) dwr16(0x09aa, 1);
  set_flag_bit(0x0796, 5, ini_int(0xc6, 0));
  set_flag_bit(0x0796, 6, ini_int(0xc7, 0));
  load_fields(0xb5, 0x164e);
  load_sort(0xb6, 0x165a, 0x1658);
  load_fields(0xb7, 0x1684);
  load_sort(0xb8, 0x1690, 0x168e);
  load_fields(0xb9, 0x16ba);
  load_sort(0xba, 0x16c6, 0x16c4);
  load_fields(0xbb, 0x16f0);
  load_sort(0xbc, 0x16fc, 0x16fa);
  /* "ReportDefGraph": the low nibble of DS:0x0796 (0..7) */
  v = ini_int(0xbd, 7);
  dwr16(0x0796, (uint16_t)(drd16(0x0796) ^ (((uint8_t)v ^ drd8(0x0796)) & 0xf)));
  load_string(key, 0xba);
  if ((drd8(0x0796) & 0xf) > 7) dwr16(0x0796, (uint16_t)((drd16(0x0796) & 0xfff7) | 7));
  dwr16(0x15c2, ini_int(0xc0, 1));
  /* "HistoryInfo": the date as a day number (31 * (12 * year + month) + day) against the stored one */
  (void)CALL_ORIG(34, 0x0450, FAR_CDECL, 1, 0x5866);
  dwr8(0x586b, 0);
  dwr8(0x5868, 0);
  {
    uint16_t a = (uint16_t)CALL_ORIG(34, 0x037a, FAR_CDECL, 1, 0x586c);
    uint16_t si = (uint16_t)((uint16_t)(a + a + a) << 2);
    si = (uint16_t)(si + (uint16_t)CALL_ORIG(34, 0x037a, FAR_CDECL, 1, 0x5866));
    si = (uint16_t)(si * 0x1f);
    si = (uint16_t)(si + (uint16_t)CALL_ORIG(34, 0x037a, FAR_CDECL, 1, 0x5869));
    ax = ini_int(0xbf, -1);
    dwr16(0x27f4, ax);
    if (si < ax) {
      ax = si;
      dwr16(0x27f4, si);
    }
    v = (uint16_t)((uint16_t)(ax + 0x1f) <= si);
    dwr16(0x0794, (uint16_t)(drd16(0x0794) ^ (((uint8_t)(v + v) ^ drd8(0x0794)) & 2)));
  }
  /* section "Fonts": four face names of 5..31 characters */
  load_string(app, 0xc1);
  for (i = 0; i < 4; i++) {
    (void)ini_str((uint16_t)(0xc2 + i), 0x076b, 0x5866, 0x50);
    len = str_len(MEM_SEL_DGROUP, 0x5866);
    if ((int16_t)len >= 5 && (int16_t)len <= 0x1f) copy_cstr((uint16_t)(0x06ca + 0x20 * i), 0x5866);
  }
  /* section "ZipOrders": keys "ZipOrders1".."ZipOrders4" pack 20 letters a..p into five words and keep
   * the rest of the string (up to 12 characters) as a name; records of 0x18 bytes at DS:0x5448 */
  load_string(app, 0x93);
  for (i = 0; i < 0x18; i++) dwr32((uint16_t)(0x5448 + 4 * i), 0);
  {
    uint16_t rec = 0x5448;
    for (i = 0; rec < 0x54a8; i++, rec = (uint16_t)(rec + 0x18)) {
      copy_cstr(key, app);
      len = str_len(MEM_SEL_DGROUP, key);
      dwr8((uint16_t)(key + len), (uint8_t)(i + 0x31));
      dwr8((uint16_t)(key + len + 1), 0);
      (void)GetPrivateProfileString(MK_FP(rt_ss, app), MK_FP(rt_ss, key), dsfp(0x076c), dsfp(0x5866), 0x50, MK_FP(rt_ss, ini_file()));
      len = str_len(MEM_SEL_DGROUP, 0x5866);
      if (!((int16_t)len < 0x14 || (int16_t)len > 0x20)) {
        uint16_t p = 0x5866, d = 0;
        while (d < 0x14) {
          int8_t c = (int8_t)drd8(p);
          if (c < 0x61 || c > 0x70) break;
          p = (uint16_t)(p + 1);
          d++;
        }
        if (d >= 0x14) {
          uint16_t si = 0x5866, di = rec;
          for (uint16_t cnt = 0; cnt < 5; cnt++) {
            uint16_t c1 = (uint16_t)(int16_t)(int8_t)drd8(si);
            uint16_t w = (uint16_t)((uint16_t)(c1 << 12) ^ drd16(di));
            w &= 0x0fff;
            w ^= (uint16_t)((uint16_t)(c1 - 1) << 12);
            dwr16(di, w);
            si++;
            w = (uint16_t)((uint16_t)((int16_t)(int8_t)drd8(si) - 0x61) ^ drd16(di));
            w &= 0x0fff;
            dwr16(di, (uint16_t)(drd16(di) ^ w));
            si++;
            w = (uint16_t)((uint8_t)(drd8(si) - 0x61));
            dwr16(di, (uint16_t)(drd16(di) | (uint16_t)(w << 4)));
            si++;
            w = (uint16_t)((uint16_t)((uint8_t)(drd8(si) - 1)) << 8);
            dwr16(di, (uint16_t)(drd16(di) | (w & 0x0f00)));
            si++;
            di = (uint16_t)(di + 2);
          }
          copy_cstr((uint16_t)(rec + 0x0a), si);
          dwr8((uint16_t)(rec + 0x17), 1);
        }
      }
    }
  }
  /* "ZipOrdersP1".."ZipOrdersP5": a letter pair a..m gives the number of 4-letter groups, then the
   * groups (decoded into words) and a name; records of 0x28 bytes at DS:0x2576 */
  for (i = 0; i < 0x32; i++) dwr32((uint16_t)(0x2576 + 4 * i), 0);
  {
    uint16_t rec = 0x2576;
    for (i = 0; rec < 0x263e; i++, rec = (uint16_t)(rec + 0x28)) {
      copy_cstr(key, app);
      len = str_len(MEM_SEL_DGROUP, key);
      dwr8((uint16_t)(key + len), 0x50);
      dwr8((uint16_t)(key + len + 1), (uint8_t)(i + 0x31));
      dwr8((uint16_t)(key + len + 2), 0);
      (void)GetPrivateProfileString(MK_FP(rt_ss, app), MK_FP(rt_ss, key), dsfp(0x076d), dsfp(0x5866), 0x50, MK_FP(rt_ss, ini_file()));
      len = str_len(MEM_SEL_DGROUP, 0x5866);
      if ((int16_t)len >= 3 && (int16_t)len <= 0x40) {
        int16_t cnt = (int16_t)((int16_t)(int8_t)drd8(0x5867) - 0x61);
        if (cnt >= 0 && cnt <= 0x0c) {
          int16_t need = (int16_t)(cnt * 4 + 2);
          uint16_t di = 0x5866;
          int16_t matched = 0;
          for (;;) {
            int8_t c = (int8_t)drd8(di);
            if (c < 0x61 || c > 0x70) break;
            di = (uint16_t)(di + 1);
            matched++;
            if (!(need > matched)) break;
          }
          if (!(need > matched)) {
            if (str_len(MEM_SEL_DGROUP, di) <= 0x0c) {
              copy_cstr(rec, di);
              dwr8((uint16_t)(rec + 0x0e), (uint8_t)(drd8(0x5866) != 0x61));
              dwr8((uint16_t)(rec + 0x0d), 1);
              dwr8((uint16_t)(rec + 0x0f), (uint8_t)cnt);
              if (cnt > 0) {
                uint16_t si = (uint16_t)(rec + 0x10), p = 0x5868;
                for (int16_t g = cnt; g > 0; g--) {
                  uint16_t w;
                  dwr16(si, (uint16_t)((int16_t)(int8_t)drd8(p) - 0x61));
                  p++;
                  w = (uint16_t)((uint16_t)((int16_t)(int8_t)drd8(p) - 0x61) << 4);
                  dwr16(si, (uint16_t)(drd16(si) | w));
                  p++;
                  p++;
                  w = (uint16_t)((uint16_t)(uint8_t)(drd8((uint16_t)(p - 1)) - 0x61) << 8);
                  dwr16(si, (uint16_t)(drd16(si) | w));
                  w = (uint16_t)((uint16_t)((int16_t)(int8_t)drd8(p) - 1) << 12);
                  p++;
                  dwr16(si, (uint16_t)(drd16(si) | w));
                  if ((uint16_t)((drd16(si) & 0xff00) | (drd8(si) & 0xc0)) > 0xff00)
                    dwr16(si, (uint16_t)((drd8(si) & 0x3f) | 0x40));
                  if ((drd8(si) & 0x3f) >= 7) dwr8(si, (uint8_t)(drd8(si) & 0xc0));
                  si = (uint16_t)(si + 2);
                }
              }
            }
          }
        }
      }
    }
  }
  load_string(0x2576, 0x022a);
  dwr8(0x2583, 1);
  FRAME_LEAVE();
}

/* 1:0x0af8 (Ghidra FUN_1000_0af8), 187 bytes. ENTER 8,0; PUSH DI; PUSH SI. FAR CDECL: nCmdShow
 * ([BP+6]); AX = 1 on success, 0 when the window or an accelerator table could not be had.
 * ANDs DS:0x251a with 0xfe1a (bits 0, 2, 5, 6, 7 and 8 cleared), sets
 * DS:0x2520 = 0xffff, runs the ini reader 1:0x0d90 (it mirrors a frame: through rt_call_far), then
 * CreateWindow(DS:0x0196 "starsframe", DS:0x0753 "Stars!", WS_OVERLAPPEDWINDOW 0x00cf0000, the
 * rectangle read into DS:0x2510 (x, y, width, height), no parent, no menu, hInstance DS:0x54f4)
 * -> DS:0x2806, LoadAccelerators(0x74) -> DS:0x2666, LoadAccelerators(0x438) -> DS:0x4c72 (zero stops
 * the function with 0 at once). nCmdShow 1 becomes 3 when the placement flags DS:0x2518 & 3 are not 0.
 * The window is shown with that command and then hidden again with ShowWindow(h, 0), both calls as in
 * the original. The 8 bytes at BP-8 are a copy of the rectangle (REP MOVSD), pushed word by word. */
DECOMP(1, 0x0af8, fn_01_0af8, FAR_CDECL)
uint16_t fn_01_0af8(int16_t cmd_show)
{
  FRAME_ENTER(8 + 4);
  uint16_t result = 0;
  dwr16(0x251a, (uint16_t)(drd16(0x251a) & 0xfe1a));
  dwr16(0x2520, 0xffff);
  (void)rt_call_far(MK_FP(SEG_SEL(1), 0x0d90), FAR_CDECL, NULL, 0);
  const int16_t x = (int16_t)drd16(0x2510), y = (int16_t)drd16(0x2512);
  const int16_t cx = (int16_t)drd16(0x2514), cy = (int16_t)drd16(0x2516);
  const uint16_t hwnd = CreateWindow(dsfp(0x0196), dsfp(0x0753), 0x00cf0000, x, y, cx, cy, 0, 0, drd16(0x54f4), 0);
  dwr16(0x2806, hwnd);
  if (hwnd != 0) {
    dwr16(0x2666, LoadAccelerators(drd16(0x54f4), 0x74));
    if (drd16(0x2666) != 0) {
      dwr16(0x4c72, LoadAccelerators(drd16(0x54f4), 0x438));
      if (drd16(0x4c72) != 0) {
        int16_t cmd = cmd_show;
        if (cmd_show == 1) cmd = ((drd8(0x2518) & 3) == 0) ? 1 : 3;
        ShowWindow(drd16(0x2806), cmd);
        ShowWindow(drd16(0x2806), 0);
        result = 1;
      }
    }
  }
  FRAME_LEAVE();
  return result;
}

/* ---- 1:0x0000 tables. Each row is one call of the original with its immediate arguments and the DGROUP
 * word that receives the result; the calls are made in this order, interleaved as 1:0x0000 does. ---- */
typedef struct DsSlot {
  uint32_t value;   /* colour, or the resource name as the API takes it (MAKEINTRESOURCE or a DGROUP string) */
  uint16_t dest;    /* DGROUP offset of the result */
} DsSlot;

/* 8:0xa4ae(colour): a solid brush. The first 11 rows come before the clear of DS:0x5750. */
static const DsSlot brushes1[] = {
    {0x00ff00, 0x54fc}, {0x00ffff, 0x59ce}, {0xff0000, 0x5a5c}, {0x0000ff, 0x5a58}, {0x00ffff, 0x26b0}, {0x0000ff, 0x0024},
    {0x7f0000, 0x0028}, {0x007f00, 0x0026}, {0x00007f, 0x27d4}, {0x7f007f, 0x290c}, {0x9fffff, 0x29d4}};
static const DsSlot brushes2[] = {
    {0xff0000, 0x27f6}, {0x007f00, 0x27f8}, {0x00ffff, 0x27fa}, {0xffffff, 0x27fc}, {0x0000ff, 0x27fe}, {0x7f0000, 0x5742},
    {0xff0000, 0x5744}, {0x00007f, 0x5746}, {0x0000ff, 0x5748}, {0x007f00, 0x574a}, {0x00ff00, 0x574c}, {0xff0000, 0x5754},
    {0x7f0000, 0x5756}, {0x00ff00, 0x5758}, {0x007f00, 0x575a}, {0x00ffff, 0x575c}, {0x007f7f, 0x575e}, {0x0000ff, 0x5760},
    {0x00007f, 0x5762}, {0x00ffff, 0x2546}, {0x007f7f, 0x5380}, {0xc0c0c0, 0x0020}, {0x808080, 0x0022}};

/* CreatePen(style, 1, colour). The fifth pen of the first group is dotted (style 2). */
typedef struct PenSlot {
  uint32_t color;
  uint16_t dest;
  uint16_t style;
} PenSlot;
static const PenSlot pens1[] = {{0x00ff00, 0x6664, 0}, {0x007f00, 0x553c, 0}, {0xff0000, 0x5a42, 0}, {0x0000ff, 0x574e, 0}, {0x7f7f7f, 0x5a60, 2}, {0x00007f, 0x59f8, 0}};
static const PenSlot pens2[] = {{0x7f0000, 0x5752, 0}, {0x00ffff, 0x293e, 0}, {0x007f7f, 0x2648, 0}, {0x7f007f, 0x293c, 0}};

/* LoadCursor(hInstance, name) and LoadIcon: DS_STR(offset) names a string in DGROUP, a small value is an id. */
#define DS_STR(off) (((uint32_t)MEM_SEL_DGROUP << 16) | (off))
static const DsSlot cursors[] = {
    {DS_STR(0x0627), 0x4c80}, {DS_STR(0x0632), 0x5864}, {DS_STR(0x063d), 0x4d94}, {DS_STR(0x0649), 0x54e8}, {0x7a, 0x2804}, {0x79, 0x415e},
    {0x102, 0x2670}, {0x104, 0x2940}, {0x107, 0x53b2}, {0x108, 0x4ac8}, {0x109, 0x5442}};
static const DsSlot icons[] = {
    {DS_STR(0x0672), 0x0316}, {DS_STR(0x067b), 0x0318}, {DS_STR(0x0683), 0x031a}, {DS_STR(0x068b), 0x2760}, {DS_STR(0x0694), 0x2762},
    {DS_STR(0x069d), 0x2764}, {DS_STR(0x06a6), 0x2766}, {DS_STR(0x06af), 0x2768}, {DS_STR(0x06b8), 0x276a}, {DS_STR(0x06c1), 0x276c}};

/* MakeProcInstance(segment:offset, hInstance) -> the far result at dest (and dest + 2). */
typedef struct ProcSlot {
  uint8_t seg;
  uint16_t off;
  uint16_t dest;
} ProcSlot;
static const ProcSlot procs[] = {{13, 0x14bc, 0x27be}, {13, 0x1508, 0x29c2}, {10, 0x674e, 0x27b2}, {25, 0x4632, 0x2682},
                                 {4, 0x4dba, 0x53e2},  {27, 0x1604, 0x53b4}, {32, 0x0000, 0x4176}, {8, 0xb96a, 0x2688}};

/* LoadBitmap, CreatePatternBrush, DeleteObject: a pattern brush from the bitmap `name`; returns the brush. */
static uint16_t pattern_brush(uint32_t name)
{
  uint16_t bm = LoadBitmap(drd16(0x54f4), name);
  uint16_t brush = CreatePatternBrush(bm);
  DeleteObject(bm);
  return brush;
}

/* 8:0xad28(id): loads the resource id (a string or bitmap holder); the result goes to DS:dest. */
static uint16_t load_res(uint16_t id, uint16_t dest)
{
  uint16_t h = (uint16_t)CALL_ORIG(8, 0xad28, FAR_CDECL, 1, id);
  dwr16(dest, h);
  return h;
}

/* 12:0x0232(size, flags) through the frame-mirroring call; the far result goes to DS:dest. */
static void alloc_far(uint16_t size, uint16_t flags, uint16_t dest)
{
  const uint16_t args[2] = {size, flags};
  uint32_t p = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, args, 2);
  dwr16(dest, (uint16_t)p);
  dwr16((uint16_t)(dest + 2), (uint16_t)(p >> 16));
}

/* 1:0x0000 (Ghidra FUN_1000_0000), 2192 bytes. ENTER 8,0; PUSH DI; PUSH SI. FAR CDECL, no arguments;
 * AX = 1 when everything loaded, else 0 after the box 8:0x8d94(0x17, 0x10). The start-up of the game:
 *  - the display class in the top bits of DS:0x0791: below 800x600 both clear, below 1024x768 bit 6,
 *    below 1111x888 bit 7, else both (screen size from GetSystemMetrics 0 and 1; the signed compares
 *    are per axis); then DS:0x0790 &= 0xfb7f; 192 bytes DS:0x0de2 -> DS:0x51b4;
 *  - two regions (CreateRectRgn(-10,-10,2000,2000) -> DS:0x543e, (0,0,10,10) -> DS:0x253e);
 *  - the solid brushes (8:0xa4ae) and pens, the pattern brushes of bitmaps (DS:0x60a ... and ids
 *    0x1cc..0x1ce -> DS:0x59e0..), cursors, bitmaps, icons, 8:0xad28 resource loads (any 0 sets the failure
 *    flag BP-8), the palette 8:0xa910(DS:0x4d8e), 12:0x0232 blocks, MakeProcInstance for the eight
 *    callbacks, 8:0xb6b6.
 * Success also needs the bitmaps DS:0x4c74, 0x4174, 0x275c, 0x4d98, 0x2884, 0x5a54, the loaded
 * resources DS:0x565a, 0x2880, 0x287e and the icons DS:0x316, 0x318, 0x31a to be non-zero. */
DECOMP(1, 0x0000, fn_01_0000, FAR_CDECL)
uint16_t fn_01_0000(void)
{
  FRAME_ENTER(8 + 4);
  uint16_t failed = 0;   /* BP-8 */
  uint16_t i, h;
  int16_t cx = GetSystemMetrics(0);
  int16_t cy = GetSystemMetrics(1);
  if (cx < 0x320 || cy < 0x258) dwr8(0x0791, (uint8_t)(drd8(0x0791) & 0x3f));
  else if (cx < 0x400 || cy < 0x300) dwr8(0x0791, (uint8_t)((drd8(0x0791) & 0x3f) | 0x40));
  else if (cx < 0x457 || cy < 0x378) dwr8(0x0791, (uint8_t)((drd8(0x0791) & 0x3f) | 0x80));
  else dwr8(0x0791, (uint8_t)(drd8(0x0791) | 0xc0));
  dwr16(0x0790, (uint16_t)(drd16(0x0790) & 0xfb7f));
  for (i = 0; i < 0x30; i++) dwr32((uint16_t)(0x51b4 + 4 * i), drd32((uint16_t)(0x0de2 + 4 * i)));
  dwr16(0x543e, CreateRectRgn(-10, -10, 2000, 2000));
  dwr16(0x253e, CreateRectRgn(0, 0, 10, 10));
  for (i = 0; i < sizeof brushes1 / sizeof brushes1[0]; i++)
    dwr16(brushes1[i].dest, (uint16_t)CALL_ORIG(8, 0xa4ae, FAR_CDECL, 2, (uint16_t)brushes1[i].value, (uint16_t)(brushes1[i].value >> 16)));
  dwr16(0x5750, 0);
  for (i = 0; i < sizeof brushes2 / sizeof brushes2[0]; i++)
    dwr16(brushes2[i].dest, (uint16_t)CALL_ORIG(8, 0xa4ae, FAR_CDECL, 2, (uint16_t)brushes2[i].value, (uint16_t)(brushes2[i].value >> 16)));
  for (i = 0; i < sizeof pens1 / sizeof pens1[0]; i++)
    dwr16(pens1[i].dest, CreatePen((int16_t)pens1[i].style, 1, pens1[i].color));
  dwr16(0x5740, 0);
  for (i = 0; i < sizeof pens2 / sizeof pens2[0]; i++)
    dwr16(pens2[i].dest, CreatePen((int16_t)pens2[i].style, 1, pens2[i].color));
  dwr16(0x4d92, pattern_brush(dsfp(0x060a)));
  for (i = 0; i < 3; i++) dwr16((uint16_t)(0x59e0 + 2 * i), pattern_brush((uint32_t)(i + 0x1cc)));
  dwr16(0x53ee, pattern_brush(dsfp(0x0616)));
  dwr16(0x5446, pattern_brush(dsfp(0x061f)));
  for (i = 0; i < sizeof cursors / sizeof cursors[0]; i++)
    dwr16(cursors[i].dest, LoadCursor(drd16(0x54f4), cursors[i].value));
  dwr16(0x4c74, LoadBitmap(drd16(0x54f4), dsfp(0x0656)));
  dwr16(0x4d98, LoadBitmap(drd16(0x54f4), 0x58));
  dwr16(0x4174, LoadBitmap(drd16(0x54f4), dsfp(0x0661)));
  dwr16(0x5a3c, LoadBitmap(drd16(0x54f4), 0xf9));
  (void)load_res(0x70, 0x4b6c);
  (void)load_res(0x57, 0x26ae);
  h = load_res(0xb2, 0x5a4e);
  if (drd16(0x4b6c) == 0 || drd16(0x26ae) == 0 || h == 0) failed = 1;
  for (i = 0; i < 5; i++) {
    if (load_res((uint16_t)(i + 0x228), (uint16_t)(0x4d88 + 2 * i)) == 0) failed = 1;
    if (load_res((uint16_t)(i + 0x22d), (uint16_t)(0x5344 + 2 * i)) == 0) failed = 1;
  }
  for (i = 0; i < 7; i++)
    if (load_res((uint16_t)(i + 0x1f4), (uint16_t)(0x2808 + 2 * i)) == 0) failed = 1;
  dwr16(0x54f8, (uint16_t)CALL_ORIG(8, 0xa910, FAR_CDECL, 1, drd16(0x4d8e)));
  (void)load_res(0x85, 0x565a);
  (void)load_res(0x50, 0x2880);
  (void)load_res(0x4f, 0x287e);
  dwr16(0x275c, LoadBitmap(drd16(0x54f4), 0x77));
  dwr16(0x2884, LoadBitmap(drd16(0x54f4), 0x86));
  dwr16(0x5a54, LoadBitmap(drd16(0x54f4), 0xc7));
  (void)load_res(0x437, 0x0c7c);
  for (i = 0; i < sizeof icons / sizeof icons[0]; i++)
    dwr16(icons[i].dest, LoadIcon(drd16(0x54f4), icons[i].value));
  alloc_far(0x7d00, 4, 0x096c);
  alloc_far(0xffc8, 2, 0x0aac);
  for (i = 0; i < sizeof procs / sizeof procs[0]; i++) {
    uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(procs[i].seg), procs[i].off), drd16(0x54f4));
    dwr16(procs[i].dest, (uint16_t)pi);
    dwr16((uint16_t)(procs[i].dest + 2), (uint16_t)(pi >> 16));
  }
  (void)CALL_ORIG(8, 0xb6b6, FAR_CDECL, 0);
  alloc_far(0x0800, 9, 0x53ea);
  alloc_far(0x0800, 9, 0x176e);
  alloc_far(0x0800, 9, 0x1772);
  alloc_far(0x0800, 9, 0x1776);
  if (failed == 0 && drd16(0x4c74) != 0 && drd16(0x4174) != 0 && drd16(0x275c) != 0 && drd16(0x565a) != 0 &&
      drd16(0x2880) != 0 && drd16(0x287e) != 0 && drd16(0x5a54) != 0 && drd16(0x4d98) != 0 && drd16(0x2884) != 0 &&
      drd16(0x0318) != 0 && drd16(0x0316) != 0 && drd16(0x031a) != 0) {
    FRAME_LEAVE();
    return 1;
  }
  (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x17, 0x10);
  FRAME_LEAVE();
  return 0;
}
