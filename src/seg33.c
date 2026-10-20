/* Decompiled code segment 33 (NE segment 33, selector 0x0108).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg33.c; Ghidra's pseudo-C in re/decomp/seg33 is only a reading aid.
 *
 * Segment 33 holds the map-object (minefield) code. The map objects are the 18-byte records of
 * struct MapObject, kept sorted by their first word in a far array: base DS:0x188E/0x1890, count
 * DS:0x1892, capacity DS:0x1894. The first word is (kind << 13) | (owner << 9) | serial: bits 9-12
 * the owning player, bits 13-15 the kind, bits 0-8 a serial that makes the key unique.
 *   33:0x0000  insert a record (keeps the array sorted, grows it by 10 records when full)
 *   33:0x015a  delete a record
 *   33:0x01a0  count the planets within a radius (planet table DS:0x31BA sorted by x)
 *   33:0x02d6  draw one line of text of the minefield window (GDI + wsprintf)
 *   33:0x0456  check a position for a new minefield (0xF = not allowed, else a "too close" mask)
 *   33:0x0742  level of a record from its word +6
 *   33:0x0784  the turn processing of the minefields: fleets inside them are hit
 *   33:0x1194  record a tech flag for a player and pick a message and a string word
 *
 * The functions that call out mirror their original frames (CONVENTIONS rule d): FRAME_ENTER(n +
 * 2 * pushes) with n the ENTER size and one push for each PUSH SI / PUSH DI. A callee that is
 * decompiled and mirrors a frame (12:0x0232, 12:0x03ec) is reached through rt_call_far. Callees
 * that are not decompiled yet use CALL_ORIG. Memory is reached in 16-bit pieces: an offset is a
 * uint16_t and wraps like the original's. DS = SS = DGROUP in every function here (the few
 * instructions that load another DS restore it before the next DGROUP access). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/fp80.h"
#include "runtime/gdi.h"
#include "runtime/text.h"
#include "runtime/user.h"
#include "src/stars.h"
#include "src/crt34.h"

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
/* DGROUP (DS) words, bytes and dwords. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
static inline void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }

/* Player table: DS:0x5A64 + 0xC0 * player (struct Player in src/stars.h). The offsets used here
 * are fields that stars.h still lists as padding; they are named by their DS address in the
 * comments: +0x06 DS:0x5A6A (word: record_flags, status_flags), +0x1A..+0x1F DS:0x5A7E (six signed
 * bytes, the player's six levels), +0x20 DS:0x5A84 (six dwords, one per level), +0x52 DS:0x5AB6
 * (word, a mask of flags), +0x54 DS:0x5AB8 (byte). */
static inline uint16_t pl(uint16_t player, uint16_t field) {
  return (uint16_t)(DS_PLAYERS + (uint16_t)((uint32_t)player * 0xc0u) + field);
}

/* 33:0x0742 (Ghidra FUN_1100_0742), 65 bytes. PUSH BP; MOV BP,SP; PUSH DI. A leaf.
 * fn_33_0742(obj): FAR CDECL, far pointer to a record at [BP+6]; returns AX. From the word at +6:
 * level = ((w & 0xFFC) >> 2) / 5 + (w & 3) - 2, clamped to 0..6. The division is IDIV with the
 * dividend zero-extended (it is at most 0x3FF), so it never traps. */
DECOMP(33, 0x0742, fn_33_0742, FAR_CDECL)
int16_t fn_33_0742(uint32_t obj)
{
  uint16_t w = rd16(FP_SEG(obj), (uint16_t)(FP_OFF(obj) + 6));
  uint16_t low = (uint16_t)(w & 3);
  uint16_t q = (uint16_t)(((uint16_t)(w & 0xffc) >> 2) / 5);
  int16_t bx = (int16_t)(uint16_t)(q + low);
  bx = (int16_t)(uint16_t)((uint16_t)bx - 2);
  if (bx < 0) return 0;
  if (bx > 6) bx = 6;
  return bx;
}

/* 33:0x1194 (Ghidra FUN_1100_1194), 170 bytes. ENTER 2,0; PUSH SI. A leaf (the local is not
 * exposed). fn_33_1194(flag, player, out): FAR CDECL, flag at [BP+6], player at [BP+8], out (a DS
 * offset) at [BP+0xA]. ORs flag into the player's word DS:0x5AB6 + 0xC0 * player. For a flag that
 * is a single bit from 2 to 0x800 it stores a packed word (a string pair, high byte 0xC0..0xCF) at
 * DS:out and returns the message number 0x10B (0x10C for flag 0x100, 0x10F for 0x400). Any other
 * flag value stores 0xCC04 and returns 0x10B. */
DECOMP(33, 0x1194, fn_33_1194, FAR_CDECL)
uint16_t fn_33_1194(uint16_t flag, int16_t player, uint16_t out)
{
  uint16_t off = pl((uint16_t)player, 0x52);
  dwr16(off, (uint16_t)(drd16(off) | flag));
  uint16_t msg = 0x10b;
  uint16_t word;
  switch (flag) {
    case 0x40: word = 0xc507; break;
    case 0x20: word = 0xc608; break;
    case 0x02: word = 0xcb04; break;
    case 0x04: word = 0xc206; break;
    case 0x08: word = 0xc309; break;
    case 0x10: word = 0xc706; break;
    case 0x80: word = 0xc412; break;
    case 0x100: msg = 0x10c; word = 0xce1e; break;
    case 0x200: word = 0xc008; break;
    case 0x400: msg = 0x10f; word = 0xcf0e; break;
    case 0x800: word = 0xcc09; break;
    default: word = 0xcc04; break;
  }
  dwr16(out, word);
  return msg;
}

/* 33:0x015a (Ghidra FUN_1100_015a), 70 bytes. PUSH BP; MOV BP,SP; PUSH SI.
 * fn_33_015a(off, seg): FAR CDECL, near-pointer part of a record at [BP+6], segment at [BP+8].
 * Deletes that record of the map-object array: when it is not the last one the records behind it
 * move down by one (34:0x0c98 memmove, length ((base - off) / 0x12 + count) * 0x12 - 0x12), then
 * the count DS:0x1892 drops by one. (base - off) is a 16-bit difference divided as a signed
 * IDIV. */
DECOMP(33, 0x015a, fn_33_015a, FAR_CDECL)
void fn_33_015a(uint16_t off, uint16_t seg)
{
  FRAME_ENTER(2);
  uint16_t count = g_map_object_count;
  uint16_t last = (uint16_t)((uint16_t)(count * 0x12u) + g_map_objects - 0x12);
  if (last > off) {
    int16_t diff = (int16_t)(uint16_t)(g_map_objects - off);
    uint16_t q = (uint16_t)(diff / 0x12);
    uint16_t len = (uint16_t)((uint32_t)(uint16_t)(q + g_map_object_count) * 0x12u);
    len = (uint16_t)(len - 0x12);
    CALL_ORIG(34, 0x0c98, FAR_CDECL, 5, off, seg, (uint16_t)(off + 0x12), seg, len);
  }
  g_map_object_count = (uint16_t)(g_map_object_count - 1);
  FRAME_LEAVE();
}

/* 33:0x0000 (Ghidra FUN_1100_0000), 346 bytes. ENTER 0x18,0; PUSH DI; PUSH SI.
 * fn_33_0000(player, kind): FAR CDECL, player at [BP+6], kind at [BP+8]. Inserts a new, zeroed
 * map object into the sorted array and returns its far pointer (DX:AX). The key is
 * ((player << 9) & 0x1E00) | (kind << 13) in the top three bits, plus a serial in bits 0-8: when
 * records with the same key (ignoring the serial) exist the serial is bumped past theirs. With
 * 0x1FF records of one key, or with 0xFD2 records in all, it returns 0 (DX:AX = 0). The array
 * grows by 10 records whenever the capacity DS:0x1894 is not above the count: from nothing it
 * calls 12:0x0232 (heap 10), else 12:0x03ec; the records from the insert position on move up by
 * one record (34:0x0c98). The new record is copied into place (18 bytes, 4 dwords and a word). */
DECOMP(33, 0x0000, fn_33_0000, FAR_CDECL)
uint32_t fn_33_0000(int16_t player, int16_t kind)
{
  FRAME_ENTER(0x18 + 4);
  if ((int16_t)g_map_object_count >= 0xfd2) {
    FRAME_LEAVE();
    return 0;
  }
  struct MapObject rec;
  memset(&rec, 0, sizeof rec);
  uint16_t cx = (uint16_t)((uint16_t)player << 9);
  cx = (uint16_t)(cx & 0x1e00);
  rec.id_kind = (uint16_t)(rec.id_kind ^ cx);
  uint16_t bx = (uint16_t)((uint16_t)kind << 13);
  cx = bx;
  bx = (uint16_t)(bx ^ rec.id_kind);
  bx = (uint16_t)(bx & 0x1fff);
  cx = (uint16_t)(cx ^ bx);
  rec.id_kind = cx;

  uint16_t seg = g_map_objects_segment_maybe;
  uint16_t off = g_map_objects;
  uint16_t idx = 0;
  if ((int16_t)g_map_object_count > 0) {
    for (;;) {
      if (rd16(seg, off) >= rec.id_kind) break;
      off = (uint16_t)(off + 0x12);
      idx = (uint16_t)(idx + 1);
      if (!((int16_t)idx < (int16_t)g_map_object_count)) break;
    }
  }
  if ((int16_t)g_map_object_count > (int16_t)idx && rd16(seg, off) == rec.id_kind) {
    uint16_t serial = (uint16_t)(rd16(seg, off) & 0x1ff);
    while ((int16_t)g_map_object_count > (int16_t)idx) {
      if ((int16_t)serial >= 0x1ff) {
        FRAME_LEAVE();
        return 0;
      }
      if (rd16(seg, off) != rec.id_kind) break;
      uint16_t t = (uint16_t)((uint16_t)(rec.id_kind + 1) ^ rec.id_kind);
      t = (uint16_t)((t & 0xff) | (t & 0x100));
      rec.id_kind = (uint16_t)(rec.id_kind ^ t);
      off = (uint16_t)(off + 0x12);
      serial = (uint16_t)(serial + 1);
      idx = (uint16_t)(idx + 1);
      if (!((int16_t)idx < (int16_t)g_map_object_count)) break;
    }
  }

  if (!((int16_t)g_map_object_capacity > (int16_t)g_map_object_count)) {
    g_map_object_capacity = (uint16_t)(g_map_object_capacity + 10);
    uint32_t r;
    uint16_t size = (uint16_t)((uint32_t)g_map_object_capacity * 0x12u);
    if ((g_map_objects_segment_maybe | g_map_objects) == 0) {
      uint16_t args[2] = {size, 10};
      r = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, args, 2);
    } else {
      uint16_t args[4] = {g_map_objects, g_map_objects_segment_maybe, size, 10};
      r = rt_call_far(MK_FP(SEG_SEL(12), 0x03ec), FAR_CDECL, args, 4);
    }
    g_map_objects = FP_OFF(r);
    g_map_objects_segment_maybe = FP_SEG(r);
    off = (uint16_t)((uint16_t)((uint32_t)idx * 0x12u) + g_map_objects);
    seg = FP_SEG(r);
  }
  if ((int16_t)g_map_object_count > (int16_t)idx) {
    uint16_t len = (uint16_t)((uint32_t)(uint16_t)(g_map_object_count - idx) * 0x12u);
    CALL_ORIG(34, 0x0c98, FAR_CDECL, 5, (uint16_t)(off + 0x12), seg, off, seg, len);
  }
  g_map_object_count = (uint16_t)(g_map_object_count + 1);
  for (uint16_t i = 0; i < 0x12; i++) wr8(seg, (uint16_t)(off + i), ((const uint8_t *)&rec)[i]);
  FRAME_LEAVE();
  return MK_FP(seg, off);
}

/* 33:0x0456 (Ghidra FUN_1100_0456), 747 bytes. ENTER 0x20,0; PUSH DI; PUSH SI.
 * fn_33_0456(obj): FAR CDECL, far pointer to a map object at [BP+6]; returns AX. Checks the
 * position (x = word +2, y = word +4) where a new minefield may go. 0xF means "not allowed": x or
 * y below 1000 or above the universe limit DS:0x74 * 400 + 0x578, another map object, a planet
 * (DS:0x31BA), or a fleet (DS:0xBA, Fleet +8) on exactly that position (the object itself does not
 * count). Otherwise the result is a mask: 4 when the position is within 10 of the border, and for
 * every map object of kind 2 (id word & 0xE000 == 0x4000), other than obj itself, and for every
 * planet, the bit of the squared distance class (8 closest, then 4, 2, 1). Objects of the same
 * owner word (obj word +0xC) use the thresholds 0x19, 0x64, 0x384, 0x1324; other owners 0x10, 0x40,
 * 0xE1, 0x384; planets 0x19, 0x64, 0x190 and the limit 0x310. The distance is dx*dx + dy*dy in
 * 32 bits compared the way the original does: the high word as a signed int16, the low word
 * unsigned. */
DECOMP(33, 0x0456, fn_33_0456, FAR_CDECL)
uint16_t fn_33_0456(uint32_t obj)
{
  FRAME_ENTER(0x20 + 4);
  uint16_t poff = FP_OFF(obj), pseg = FP_SEG(obj);
  uint16_t limit = (uint16_t)((uint32_t)g_universe_size_index * 0x190u + 0x578);
  uint16_t x = rd16(pseg, (uint16_t)(poff + 2));
  uint16_t y = rd16(pseg, (uint16_t)(poff + 4));
  uint32_t xy = (uint32_t)x | ((uint32_t)y << 16);
  if ((int16_t)x < 1000 || (int16_t)y < 1000 || (int16_t)x > (int16_t)limit || (int16_t)y > (int16_t)limit) {
    FRAME_LEAVE();
    return 0xf;
  }
  uint16_t first = g_map_objects;
  uint16_t aseg = g_map_objects_segment_maybe;
  uint16_t end = (uint16_t)(first + (uint16_t)((uint32_t)g_map_object_count * 0x12u));
  if (end > first) {
    uint16_t di = first;
    do {
      if (rd32(aseg, (uint16_t)(di + 2)) == xy && !(poff == di && pseg == aseg)) {
        FRAME_LEAVE();
        return 0xf;
      }
      di = (uint16_t)(di + 0x12);
    } while (di < end);
  }
  if ((int16_t)g_planet_count_in_game > 0) {
    uint16_t bx = 0x31ba;
    uint16_t cx = 0;
    do {
      if (drd32(bx) == xy) {
        FRAME_LEAVE();
        return 0xf;
      }
      bx = (uint16_t)(bx + 4);
      cx = (uint16_t)(cx + 1);
    } while ((int16_t)cx < (int16_t)g_planet_count_in_game);
  }
  if ((int16_t)g_fleet_count > 0) {
    uint16_t bx = drd16(DS_FLEET_PTRS);
    uint16_t fseg = drd16((uint16_t)(DS_FLEET_PTRS + 2));
    uint16_t cx = 0;
    do {
      uint16_t foff = rd16(fseg, bx);
      uint16_t fsg = rd16(fseg, (uint16_t)(bx + 2));
      if ((fsg | foff) == 0) break;
      if (rd32(fsg, (uint16_t)(foff + 8)) == xy) {
        FRAME_LEAVE();
        return 0xf;
      }
      bx = (uint16_t)(bx + 4);
      cx = (uint16_t)(cx + 1);
    } while ((int16_t)cx < (int16_t)g_fleet_count);
  }
  uint16_t acc = 0;
  if ((int16_t)x < 0x3f2 || (int16_t)y < 0x3f2 || (int16_t)(uint16_t)(limit - 0xa) < (int16_t)x ||
      (int16_t)(uint16_t)(limit - 0xa) < (int16_t)y)
    acc = 4;

  uint16_t di = g_map_objects;
  uint16_t oseg = g_map_objects_segment_maybe;
  uint16_t oend = end;
  if (!(oend <= di)) {
    do {
      if ((uint8_t)(rd8(oseg, (uint16_t)(di + 1)) & 0xe0) == 0x40 && !(di == poff && oseg == pseg)) {
        int16_t ddx = (int16_t)(uint16_t)(x - rd16(oseg, (uint16_t)(di + 2)));
        int16_t ddy = (int16_t)(uint16_t)(y - rd16(oseg, (uint16_t)(di + 4)));
        uint32_t dist = (uint32_t)((int32_t)ddx * ddx) + (uint32_t)((int32_t)ddy * ddy);
        int16_t hi = (int16_t)(uint16_t)(dist >> 16);
        uint16_t lo = (uint16_t)dist;
        if (rd16(oseg, di) == rd16(pseg, (uint16_t)(poff + 0xc))) {
          if (hi > 0 || (hi == 0 && lo >= 0x1324)) goto next_obj;
          if (hi < 0 || lo < 0x19) acc |= 8;
          else if (lo < 0x64) acc |= 4;
          else if (lo < 0x384) acc |= 2;
          else acc |= 1;
        } else {
          if (hi > 0 || (hi == 0 && lo >= 0x384)) goto next_obj;
          if (hi < 0 || lo < 0x10) acc |= 8;
          else if (lo < 0x40) acc |= 4;
          else if (lo < 0xe1) acc |= 2;
          else acc |= 1;
        }
      }
    next_obj:
      di = (uint16_t)(di + 0x12);
    } while (di < oend);
  }
  if ((int16_t)g_planet_count_in_game > 0) {
    uint16_t pdi = 0x31bc;
    uint16_t n = g_planet_count_in_game;
    do {
      int16_t ddx = (int16_t)(uint16_t)(x - drd16((uint16_t)(pdi - 2)));
      int16_t ddy = (int16_t)(uint16_t)(y - drd16(pdi));
      uint32_t dist = (uint32_t)((int32_t)ddx * ddx) + (uint32_t)((int32_t)ddy * ddy);
      if (!((int32_t)dist >= 0x310)) {
        int16_t hi = (int16_t)(uint16_t)(dist >> 16);
        uint16_t lo = (uint16_t)dist;
        if (hi < 0 || (hi == 0 && lo < 0x19)) acc |= 8;
        else if (hi < 0 || lo < 0x64) acc |= 4;
        else if (hi < 0 || lo < 0x190) acc |= 2;
        else acc |= 1;
      }
      pdi = (uint16_t)(pdi + 4);
      n = (uint16_t)(n - 1);
    } while (n != 0);
  }
  FRAME_LEAVE();
  return acc;
}

/* 33:0x01a0 (Ghidra FUN_1100_01a0), 310 bytes. ENTER 0x12,0; PUSH DI; PUSH SI. Uses the x87.
 * fn_33_01a0(x, y, r2): FAR CDECL, x at [BP+6], y at [BP+8], r2 (a long, the squared radius) at
 * [BP+0xA]; returns the number of planets (DS:0x31BA, x/y word pairs sorted by x, count DS:0x7A)
 * whose squared distance to (x, y) is at most r2. The radius is r = (long)(sqrt(r2) + c) with c the
 * double at DS:0x207C (0.99999..., so r is the square root rounded up): FLD c, FLDZ, FILD r2, FST
 * ST(1), CALL 34:0x0D68 (sqrt), FADD ST(2), CALL 34:0x0DE6 (ftol), two FSTP ST(0). The first
 * planet to look at is estimated by interpolation, (x - first_x) * count / (last_x - first_x) with
 * a 32-bit IDIV (a zero span traps in the original: stopped here), clamped to 0..count-1; the scan
 * goes back while x >= x - r, then forward while the planet x <= x + r and the pointer is below
 * the end of the table, counting those with y in range and dx*dx + dy*dy <= r2 (signed 32-bit). */
DECOMP(33, 0x01a0, fn_33_01a0, FAR_CDECL)
int16_t fn_33_01a0(int16_t x, int16_t y, int32_t r2)
{
  FRAME_ENTER(0x12 + 4);
  uint64_t cbits = (uint64_t)drd32(0x207c) | ((uint64_t)drd32(0x2080) << 32);
  double cd;
  memcpy(&cd, &cbits, 8);
  Fp80 c = fp80_from_f64(cd);
  Fp80 root = crt_sqrt_34_0d68(fp80_from_i32(r2));
  uint16_t r = (uint16_t)fp80_to_i32_ftol(fp80_add(root, c));

  uint16_t xmin = (uint16_t)((uint16_t)x - r);
  uint16_t xmax = (uint16_t)(r + (uint16_t)x);
  uint16_t ymin = (uint16_t)((uint16_t)y - r);
  uint16_t ymax = (uint16_t)(r + (uint16_t)y);
  uint16_t di = 0;
  uint16_t n = g_planet_count_in_game;
  uint16_t bx = (uint16_t)(n << 2);
  uint16_t end = (uint16_t)(bx + 0x31ba);
  uint16_t first_x = drd16(0x31ba);
  int32_t span = (int32_t)(int16_t)(uint16_t)(drd16((uint16_t)(bx + 0x31b6)) - first_x);
  int32_t num = (int32_t)(int16_t)(uint16_t)((uint16_t)x - first_x) * (int32_t)(int16_t)n;
  if (span == 0) {
    fprintf(stderr, "33:0x01a0: divide error (IDIV by zero: planet table spans no x)\n");
    abort();
  }
  uint16_t si = (uint16_t)(num / span);
  if (!((int16_t)si < (int16_t)n)) si = (uint16_t)(n - 1);
  if ((int16_t)si < 0) si = 0;
  uint16_t dx = (uint16_t)((uint16_t)(si << 2) + 0x31ba);
  si = dx;
  if (!((int16_t)drd16(dx) < (int16_t)xmin)) {
    while (!(si <= 0x31ba)) {
      si = (uint16_t)(si - 4);
      if (!((int16_t)drd16(si) >= (int16_t)xmin)) break;
    }
  }
  for (;;) {
    if ((int16_t)xmax < (int16_t)drd16(si)) break;
    if (end <= si) break;
    for (;;) {
      if ((int16_t)xmin > (int16_t)drd16(si)) break;
      int16_t py = (int16_t)drd16((uint16_t)(si + 2));
      if (py < (int16_t)ymin) break;
      if (py > (int16_t)ymax) break;
      int32_t ddy = (int32_t)(int16_t)(uint16_t)((uint16_t)py - (uint16_t)y);
      uint32_t d2 = (uint32_t)(ddy * ddy);
      int32_t ddx = (int32_t)(int16_t)(uint16_t)(drd16(si) - (uint16_t)x);
      d2 += (uint32_t)(ddx * ddx);
      if ((int32_t)d2 > r2) break;
      di = (uint16_t)(di + 1);
      break;
    }
    si = (uint16_t)(si + 4);
    if (!((int16_t)drd16(si) <= (int16_t)xmax)) break;
  }
  FRAME_LEAVE();
  return (int16_t)di;
}

/* 33:0x02d6 (Ghidra FUN_1100_02d6), 383 bytes. ENTER 0x2c,0; PUSH DI; PUSH SI.
 * fn_33_02d6(hdc, prect, obj, mode): FAR CDECL, hdc at [BP+6], prect (a near pointer to a RECT) at
 * [BP+8], obj (far pointer to a map object) at [BP+0xA], mode at [BP+0xE]. Draws one text line of
 * the minefield window: selects the font DS:0x2930, asks 8:0x9874 to format the value (the
 * object's word +0xE & 0x3FFF, times 10, or the signed word +8 + 2 * mode for mode 0 to 2; mode 3
 * and 4 use the times-ten value with the unit word DS:0x16, mode 5 all three words +8 +A +C with the
 * units DS:0x27F6) and sets the background mode to transparent (1). The text is made with
 * wsprintf into DS:0x5866: "%ld" (DS:0x1896) when 8:0x9874 was given the single-value mode 1, else
 * "%ld of %ldkT" (DS:0x189C) with the 8:0x9874 result and the times-ten value. It is drawn with
 * 8:0x9318 (hdc, rect, text, length) when GetTextExtent says it is narrower than the rectangle
 * (right - left - 3). The old background mode is restored. For modes 3 and 4 the 8:0x9874 result
 * is replaced by 0. Other modes (below 0 or above 5) leave the two local arrays uninitialised in
 * the original, as they are here (the same stack bytes). wsprintf's argument words are written
 * where the original pushes them, just below the frame. */
DECOMP(33, 0x02d6, fn_33_02d6, FAR_CDECL)
void fn_33_02d6(uint16_t hdc, uint16_t prect, uint32_t obj, int16_t mode)
{
  FRAME_ENTER(0x2c + 4);
  uint16_t di = FP_OFF(obj), oseg = FP_SEG(obj);
  uint16_t si = (uint16_t)mode;
  uint16_t flag = 0;                                   /* [bp-0xe] */
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  uint16_t kind = 1;                                   /* [bp-2] */
  uint32_t v = (uint32_t)(rd16(oseg, (uint16_t)(di + 0xe)) & 0x3fff) * 10u;   /* [bp-0xa] */
  uint16_t arr = (uint16_t)(rt_bp - 0x18);             /* word array, DS offset */
  uint16_t vals = (uint16_t)(rt_bp - 0x2c);            /* dword array, DS offset */
  if ((int16_t)si >= 0 && (int16_t)si <= 4) {
    if (si == 4 || si == 3) {
      BP_LOCAL(uint16_t, -0x18) = drd16(0x16);
      BP_LOCAL(uint32_t, -0x2c) = v;
      flag = 1;
    } else {
      uint16_t bx = (uint16_t)(si + si);
      BP_LOCAL(uint16_t, -0x18) = drd16((uint16_t)(bx + 0x27f6));
      BP_LOCAL(int32_t, -0x2c) = (int16_t)rd16(oseg, (uint16_t)(di + 8 + bx));
    }
  } else if ((int16_t)si == 5) {
    uint16_t cx = (uint16_t)(di + 8);
    for (uint16_t bx = 0; (int16_t)bx < 6; bx = (uint16_t)(bx + 2)) {
      BP_LOCAL(uint16_t, -0x18 + bx) = drd16((uint16_t)(bx + 0x27f6));
      BP_LOCAL(int32_t, -0x2c + 2 * bx) = (int16_t)rd16(oseg, cx);
      cx = (uint16_t)(cx + 2);
    }
    kind = 3;
  }
  uint32_t r = CALL_ORIG(8, 0x9874, FAR_CDECL, 7, hdc, prect, kind, vals, arr, (uint16_t)v, (uint16_t)(v >> 16));
  uint32_t shown = r;                                  /* [bp-6] */
  uint16_t old = (uint16_t)SetBkMode(hdc, 1);          /* [bp-0xc] */
  if (flag != 0) shown = 0;
  int16_t n;
  if (kind == 1) {
    uint16_t va = (uint16_t)(rt_bp - 0x34);
    wr32(rt_ss, va, shown);
    n = _wsprintf(MK_FP(MEM_SEL_DGROUP, 0x5866), MK_FP(MEM_SEL_DGROUP, 0x1896), MK_FP(rt_ss, va));
  } else {
    uint16_t va = (uint16_t)(rt_bp - 0x38);
    wr32(rt_ss, va, shown);
    wr32(rt_ss, (uint16_t)(va + 4), v);
    n = _wsprintf(MK_FP(MEM_SEL_DGROUP, 0x5866), MK_FP(MEM_SEL_DGROUP, 0x189c), MK_FP(rt_ss, va));
  }
  uint32_t ext = GetTextExtent(hdc, MK_FP(MEM_SEL_DGROUP, 0x5866), n);
  uint16_t left = drd16(prect), right = drd16((uint16_t)(prect + 4));
  if ((int16_t)(uint16_t)(right - left - 3) > (int16_t)(uint16_t)ext)
    CALL_ORIG(8, 0x9318, FAR_CDECL, 4, hdc, prect, (uint16_t)0x5866, (uint16_t)n);
  (void)SetBkMode(hdc, (int16_t)old);
  FRAME_LEAVE();
}

/* 33:0x0784 (Ghidra FUN_1100_0784), 2575 bytes. ENTER 0xD0,0; PUSH DI; PUSH SI.
 * fn_33_0784(enable): FAR CDECL, enable at [BP+6] (0 does nothing). The minefield turn processing.
 * For every map object of kind 3 (id word & 0xE000 == 0x6000), in array order, it
 *  - looks at every fleet (far pointer array DS:0xBA, count DS:0x553A, a null pointer ends the
 *    list) that is not flagged (+5 bit 2) and sits on the object's x/y (Fleet +8): with a cargo sum
 *    (three dwords at Fleet +0x4C) below 5000 it reports message 0x108 once (6:0x7328, unless the
 *    fleet is flagged +5 bit 5); at or above it the fleet hits the mines: the owner's bit in the
 *    object's mask (+0xC) is set (message 0x118 when it was set already), the fleet is flagged,
 *    6:0x80fa records it, and, depending on the object's word +0xE and the owner's flag word (DS:
 *    0x5AB6 + 0xC0 * owner), the fleet is destroyed or damaged: a flag is picked at random (1194),
 *    a design/ship may be added by 7:0x1f2e / 7:0x4dcc / 7:0x2dbe / 10:0x292a, or the owner's
 *    levels (DS:0x5A7E) are raised by 6:0x7344 messages and 27:0x49ee costs.
 *  - looks at every planet (DS:0xB6, count DS:0x4D96, stride 0x38) owned by a player whose status
 *    bits allow it, within 100 of the object in x and within 10000 squared in distance, and takes
 *    cargo from the planet (Planet +0x1C..+0x24) up to a threshold (5000, or 3500 for one status
 *    value), raising the owner's levels (DS:0x5A7E) when they are below 6 * (9 or 0x19).
 * The locals are kept in the frame slots of the original (BP_LOCAL at the original offsets, named
 * by them): several slots are reused for different values and the code reads one slot after
 * another path wrote it. The registers of the original are C locals; SI and DI survive calls.
 * Calls to code not yet decompiled follow the original's argument words exactly, including the
 * words it leaves on the stack for the next call (7:0x1bca is given 3 or 8 words and the caller
 * pops only 2, the rest become the trailing arguments of the following 6:0x7328 / 6:0x7344 call). */
#define W(o) BP_LOCAL(uint16_t, (o))
#define D(o) BP_LOCAL(uint32_t, (o))
#define B8(o) BP_LOCAL(uint8_t, (o))
static inline uint16_t bit16(uint16_t n) { return (uint16_t)(1u << ((uint8_t)n & 31)); }
/* (byte at +1 of a record) & 0x1E00 >> 9: the owner player number of a fleet / map object */
static inline uint16_t owner_of(uint16_t seg, uint16_t off) {
  return (uint16_t)(((uint16_t)((uint16_t)rd8(seg, (uint16_t)(off + 1)) << 8) & 0x1e00) >> 9);
}
static inline uint16_t rand_n(uint16_t n) { return (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, n); }
static inline int8_t lvl(uint16_t off) { return (int8_t)drd8(off); }

DECOMP(33, 0x0784, fn_33_0784, FAR_CDECL)
void fn_33_0784(int16_t enable)
{
  FRAME_ENTER(0xd0 + 4);
  uint16_t ax, bx, cx, dx, si, di, es;
  uint32_t r;
  if (enable == 0) goto done;
  ax = g_map_objects;
  dx = g_map_objects_segment_maybe;
  si = ax;
  W(-0x24) = dx;
  cx = (uint16_t)((uint32_t)g_map_object_count * 0x12u);
  ax = (uint16_t)(ax + cx);
  W(-0x32) = dx;
  if (ax <= si) goto done;
  W(-0x34) = ax;
  W(-0x26) = si;
obj_loop:                                                     /* 07b4 */
  bx = W(-0x26);
  es = W(-0x24);
  if ((uint8_t)(rd8(es, (uint16_t)(bx + 1)) & 0xe0) != 0x60) goto next_obj;
  D(-0x20) = rd32(es, (uint16_t)(bx + 2));
  W(-0x22) = 0;
  if ((int16_t)g_fleet_count <= 0) goto planets;
fleet_loop:                                                   /* 07df */
  bx = (uint16_t)(W(-0x22) << 2);
  {
    uint16_t aoff = drd16(0x00ba), aseg = drd16(0x00bc);
    ax = rd16(aseg, (uint16_t)(bx + aoff));
    dx = rd16(aseg, (uint16_t)(bx + aoff + 2));
  }
  di = ax;
  W(-0x1a) = dx;
  if ((dx | ax) == 0) goto after_fleets;
  es = W(-0x1a);
  if (rd8(es, (uint16_t)(di + 5)) & 4) goto fleet_next;
  if (rd32(es, (uint16_t)(di + 8)) != D(-0x20)) goto fleet_next;
  D(-0x16) = 0;
  bx = (uint16_t)(di + 0x4c);
  W(-4) = es;
  W(-2) = 3;
  cx = W(-2);
  W(-0x1c) = di;
  do {
    si = bx;
    bx = (uint16_t)(bx + 4);
    D(-0x16) = D(-0x16) + rd32(es, si);
    cx = (uint16_t)(cx - 1);
  } while (cx != 0);
  es = W(-0x1a);
  if ((int32_t)D(-0x16) >= 0x1388) goto big;
  if (rd8(es, (uint16_t)(di + 5)) & 0x20) goto fleet_next;
  ax = rd16(es, di);
  rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(es, di), 0x108, (uint16_t)(ax | 0x8000), ax, 0}, 5);
fleet_next:                                                   /* 0871 */
  ax = g_fleet_count;
  W(-0x22) = (uint16_t)(W(-0x22) + 1);
  if ((int16_t)W(-0x22) < (int16_t)ax) goto fleet_loop;
after_fleets:                                                 /* 087e */
  W(-0x1c) = di;
planets:                                                      /* 0881 */
  ax = drd16(0x00b6);
  dx = drd16(0x00b8);
  di = ax;
  W(-0xe) = dx;
  cx = (uint16_t)((uint32_t)g_planet_count * 0x38u);
  ax = (uint16_t)(ax + cx);
  bx = ax;
  W(-0x2e) = dx;
  if (ax <= di) goto next_obj;
  W(-0x30) = ax;
planet_loop:                                                  /* 08a2 */
  es = W(-0xe);
  if (rd16(es, (uint16_t)(di + 2)) == 0xffff) goto planet_next;
  if (!(rd8(es, (uint16_t)(di + 5)) & 2)) goto planet_next;
  bx = (uint16_t)((uint32_t)rd16(es, (uint16_t)(di + 2)) * 0xc0u);
  ax = drd16((uint16_t)(bx + 0x5a6a));
  W(-0xca) = ax;
  if (!((ax >> 8) & 2)) goto planet_next;
  ax = (uint16_t)((uint16_t)((uint16_t)B8(-0xc9) << 8) & 0x1c00);
  W(-0xcc) = ax;
  if (ax < 0x800) goto planet_next;
  cx = (uint8_t)rd16(es, (uint16_t)(di + 2));
  ax = bit16(cx);
  bx = W(-0x26);
  if (rd16(W(-0x24), (uint16_t)(bx + 0xc)) & ax) goto planet_next;
  es = W(-0xe);
  bx = (uint16_t)(rd16(es, di) << 2);
  ax = (uint16_t)(drd16((uint16_t)(bx + 0x31ba)) - W(-0x20));
  dx = (ax & 0x8000) ? 0xffff : 0;
  W(-4) = ax;
  W(-2) = dx;
  if ((int16_t)dx > 0) goto next_obj;
  if ((int16_t)dx >= 0 && ax > 0x64) goto next_obj;
  bx = (uint16_t)(rd16(es, di) << 2);
  ax = (uint16_t)(drd16((uint16_t)(bx + 0x31bc)) - W(-0x1e));
  {
    int32_t dy = (int16_t)ax;
    D(-0xd0) = (uint32_t)(dy * dy);
  }
  {
    int32_t ddx = (int32_t)D(-4);
    r = (uint32_t)(ddx * ddx) + D(-0xd0);
  }
  if ((int32_t)r > 0x2710) goto planet_next;
  D(-0xc) = 0;
  ax = (uint16_t)(di + 0x1c);
  cx = W(-0xe);
  bx = ax;
  W(-4) = cx;
  W(-2) = 3;
  W(-0x10) = di;
  cx = W(-2);
  do {
    es = W(-4);
    si = bx;
    bx = (uint16_t)(bx + 4);
    D(-0xc) = D(-0xc) + rd32(es, si);
    cx = (uint16_t)(cx - 1);
  } while (cx != 0);
  di = W(-0x10);
  es = W(-0xe);
  ax = rd16(es, (uint16_t)(di + 2));
  W(-0x12) = ax;
  ax = (W(-0xcc) != 0x800) ? 0x1388 : 0xdac;                  /* 0fea / 0995 */
  dx = (ax & 0x8000) ? 0xffff : 0;
  W(-8) = ax;
  W(-6) = dx;
  if ((int16_t)dx > (int16_t)W(-0xa)) goto planet_next;
  if (!((int16_t)dx < (int16_t)W(-0xa)) && ax > W(-0xc)) goto planet_next;
  si = W(-0x26);
  es = W(-0x24);
  if (rd16(es, (uint16_t)(si + 0xe)) == 0) goto L106c;
  W(-2) = 0x32;
  ax = rd16(es, (uint16_t)(si + 0xe));
  W(-4) = ax;
  bx = (uint16_t)((uint32_t)W(-0x12) * 0xc0u);
  W(-0xc6) = bx;
  if (drd16((uint16_t)(bx + 0x5ab6)) & ax) goto L1040;
  si = ax;                                                    /* 102c */
L102e:
  if ((int16_t)W(-2) <= 0) goto L106c;
  bx = W(-0xc6);
  dwr16((uint16_t)(bx + 0x5ab6), (uint16_t)(drd16((uint16_t)(bx + 0x5ab6)) | si));
  goto L1109;
L1040:
  di = W(-2);
  si = ax;
  for (;;) {                                                  /* 1045 */
    ax = di;
    di = (uint16_t)(di - 1);
    if ((int16_t)ax <= 0) break;
    ax = rand_n(0xd);
    cx = ax;
    si = bit16(cx);
    bx = W(-0xc6);
    if (!(drd16((uint16_t)(bx + 0x5ab6)) & si)) break;
  }
  W(-2) = di;
  goto L102e;
L106c:
  si = 0;
  W(-2) = si;
  ax = (uint16_t)((uint32_t)W(-0x12) * 0xc0u);
  W(-0xc6) = ax;
  W(-4) = si;
  di = si;
  do {
    bx = W(-0xc6);
    ax = (uint16_t)(int16_t)lvl((uint16_t)(bx + si + 0x5a7e));
    di = (uint16_t)(di + ax);
    si = (uint16_t)(si + 1);
  } while ((int16_t)si < 6);
  es = W(-0x1a);
  bx = W(-0x1c);
  bx = (uint16_t)((uint32_t)rd16(es, (uint16_t)(bx + 2)) * 0xc0u);
  ax = (uint16_t)(drd8((uint16_t)(bx + 0x5ab8)) & 2);
  ax = (ax < 1) ? 0xffff : 0;
  ax = (uint16_t)(ax & 0x10);
  ax = (uint16_t)(ax + 0xa);
  ax = (uint16_t)(ax - 1);
  cx = ax;
  ax = (uint16_t)(ax + ax);
  ax = (uint16_t)(ax + cx);
  ax = (uint16_t)(ax + ax);
  if ((int16_t)ax <= (int16_t)di) goto L1172;
  ax = (uint16_t)((uint32_t)W(-0x12) * 0xc0u);
  W(-0xc6) = ax;
  W(-0xa) = 6;
  do {                                                        /* 10cb */
    W(-4) = 0;
    si = 1;
    do {
      bx = (uint16_t)(W(-0xc6) + W(-4));
      uint8_t al = drd8((uint16_t)(bx + 0x5a7e));
      bx = W(-0xc6);
      if ((int8_t)al > lvl((uint16_t)(bx + si + 0x5a7e))) W(-4) = si;
      si = (uint16_t)(si + 1);
    } while ((int16_t)si < 6);
    bx = W(-4);
    di = W(-0xc6);
    dwr8((uint16_t)(bx + di + 0x5a7e), (uint8_t)(drd8((uint16_t)(bx + di + 0x5a7e)) + 1));
    W(-0xa) = (uint16_t)(W(-0xa) - 1);
  } while (W(-0xa) != 0);
  D(-0xc) = D(-8);
L1109:
  cx = (uint8_t)W(-0x12);
  ax = bit16(cx);
  bx = W(-0x26);
  es = W(-0x24);
  wr16(es, (uint16_t)(bx + 0xc), (uint16_t)(rd16(es, (uint16_t)(bx + 0xc)) | ax));
  W(-2) = 2;
  if ((int32_t)D(-0xc) <= 0) goto L1138;
  ax = W(-0x10);
  dx = W(-0xe);
  ax = (uint16_t)(ax + 0x24);
  bx = ax;
  es = dx;                                                    /* DS = the planet segment */
  cx = W(-2);
  for (;;) {                                                  /* 1134 */
    if ((int16_t)cx < 0) goto L1138;
    {
      uint32_t eax = rd32(es, bx);
      if ((int32_t)eax > (int32_t)D(-0xc)) eax = D(-0xc);
      D(-0x16) = eax;
      wr32(es, bx, rd32(es, bx) - eax);
    }
    bx = (uint16_t)(bx - 4);
    cx = (uint16_t)(cx - 1);
    D(-0xc) = D(-0xc) - D(-0x16);
    if (!((int32_t)D(-0xc) > 0)) break;
  }
L1172:
  di = W(-0x10);
  goto planet_next;
L1138:
  di = W(-0x10);
planet_next:                                                  /* 1175 */
  di = (uint16_t)(di + 0x38);
  if (di < W(-0x30)) goto planet_loop;
next_obj:                                                     /* 1181 */
  ax = W(-0x34);
  W(-0x26) = (uint16_t)(W(-0x26) + 0x12);
  if (W(-0x26) < ax) goto obj_loop;
done:
  FRAME_LEAVE();
  return;

big:                                                          /* 099c: the fleet carries 5000 or more */
  bx = (uint16_t)((uint32_t)rd16(es, (uint16_t)(di + 2)) * 0xc0u);
  W(-0xc4) = bx;
  ax = (uint16_t)(drd8((uint16_t)(bx + 0x5ab8)) & 2);
  ax = (ax < 1) ? 0xffff : 0;
  ax = (uint16_t)((uint16_t)(ax & 0x10) + 0xa);
  W(-0x18) = ax;
  si = 0;
  do {
    ax = (uint16_t)(int16_t)lvl((uint16_t)(bx + si + 0x5a7e));
    if ((int16_t)ax < (int16_t)W(-0x18)) break;
    si = (uint16_t)(si + 1);
  } while ((int16_t)si < 6);
  W(-2) = si;
  si = W(-0x26);
  W(-4) = (W(-2) == 6) ? 1 : 0;
  bx = W(-0xc4);
  ax = drd16((uint16_t)(bx + 0x5ab6));
  W(-6) = ax;
  es = W(-0x1a);
  ax = rd16(es, (uint16_t)(di + 2));
  W(-0x12) = ax;
  cx = ax;
  ax = bit16(cx);
  es = W(-0x24);
  if (rd16(es, (uint16_t)(si + 0xc)) & ax) {                  /* 0a08: hit by this player already */
    es = W(-0x1a);
    ax = rd16(es, di);
    rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(es, di), 0x118, (uint16_t)(ax | 0x8000), ax, 0}, 5);
    goto fleet_next;
  }
  cx = (uint8_t)W(-0x12);                                     /* 0a1c */
  ax = bit16(cx);
  wr16(es, (uint16_t)(si + 0xc), (uint16_t)(rd16(es, (uint16_t)(si + 0xc)) | ax));
  es = W(-0x1a);
  ax = rd16(es, di);
  rt_call_far(MK_FP(SEG_SEL(6), 0x80fa), FAR_CDECL, (const uint16_t[]){W(-0x12), 0x4e, (uint16_t)(ax | 0x8000)}, 3);
  es = W(-0x1a);
  wr8(es, (uint16_t)(di + 5), (uint8_t)(rd8(es, (uint16_t)(di + 5)) | 4));
  es = W(-0x24);
  if (rd16(es, (uint16_t)(si + 0xe)) == 0) goto La5a;
  ax = W(-6);
  if (!(rd16(es, (uint16_t)(si + 0xe)) & ax)) goto La8a;
La5a:
  if (W(-4) == 0) goto Ldfa;
  ax = rand_n(5);
  if (ax != 0) goto La8a;
  r = CALL_ORIG(7, 0x1bca, FAR_CDECL, 3, di, W(-0x1a), ax);
  es = W(-0x1a);
  rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(es, di), 0x010e, 0xffff, (uint16_t)r, ax}, 5);
  goto fleet_next;
La8a:
  W(-2) = 0x19;
  es = W(-0x24);
  cx = rd16(es, (uint16_t)(si + 0xe));
  W(-4) = cx;
  if (cx == 0) {
    ax = rand_n(0xd);
    cx = ax;
    ax = bit16(cx);
    W(-4) = ax;
  }
  ax = W(-6);
  if (!(W(-4) & ax)) {                                        /* 0b18 */
    si = W(-2);
    di = W(-4);
  } else {
    si = W(-2);
    di = W(-4);
    for (;;) {                                                /* 0abf */
      ax = si;
      si = (uint16_t)(si - 1);
      if ((int16_t)ax <= 0) break;
      ax = rand_n(0xd);
      cx = ax;
      di = bit16(cx);
      if (!(W(-6) & di)) break;
    }
  }
  if (!((int16_t)si > 0)) di = 0x1000;                        /* 0adc */
  cx = (uint8_t)W(-0x12);
  ax = bit16(cx);
  bx = W(-0x26);
  es = W(-0x24);
  wr16(es, (uint16_t)(bx + 0xc), (uint16_t)(rd16(es, (uint16_t)(bx + 0xc)) | ax));
  if (di != 0x1000) {                                         /* 0dc4: a plain flag */
    W(-4) = di;
    si = W(-0x1c);
    di = fn_33_1194(W(-4), (int16_t)W(-0x12), (uint16_t)(rt_bp - 0xe));
    r = CALL_ORIG(7, 0x1bca, FAR_CDECL, 3, si, W(-0x1a), 0);
    es = W(-0x1a);
    rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(es, si), di, W(-0xe), (uint16_t)r, 0}, 5);
    goto Ldbd;
  }
  bx = (uint16_t)((uint32_t)W(-0x12) * 0xc0u);                /* 0afa */
  W(-0xc6) = bx;
  if (drd8((uint16_t)(bx + 0x5a6b)) & 2) goto Ldbd;
  ax = (g_current_turn > 100) ? 1 : 0;
  ax = (uint16_t)(ax - 4);
  ax = (uint16_t)(0 - ax);
  ax = rand_n(ax);
  W(-4) = ax;
  if (!((int16_t)ax < 1)) {
    ax = rand_n(2);
    ax = (uint16_t)(ax + 1);
    W(-4) = ax;
  }
  r = rt_call_far(MK_FP(SEG_SEL(2), 0x50be), FAR_CDECL, NULL, 0);                    /* 0b46: far pointer to the design templates */
  ax = FP_OFF(r);
  dx = FP_SEG(r);
  cx = (uint16_t)((uint32_t)W(-4) * 0x8du);
  ax = (uint16_t)(ax + cx);
  ax = (uint16_t)(ax + 0xa77);
  for (uint16_t i = 0; i < 0x8d; i++)
    wr8(rt_ss, (uint16_t)(rt_bp - 0xc2 + i), rd8(dx, (uint16_t)(ax + i)));
  ax = (uint16_t)CALL_ORIG(7, 0x4dcc, FAR_CDECL, 3, (uint16_t)(rt_bp - 0xc2), rt_ss, W(-0x12));
  W(-2) = ax;
  if ((int16_t)ax < 0) {
    si = (uint16_t)(W(-0x12) << 2);
    ax = drd16((uint16_t)(si + 0xbe));
    dx = drd16((uint16_t)(si + 0xc0));
    cx = (uint16_t)((uint32_t)W(-2) * 0x8du);
    ax = (uint16_t)(ax + cx);
    ax = (uint16_t)(ax + 0x7b);
    bx = ax;
    es = dx;
    si = W(-2);
    do {
      bx = (uint16_t)(bx + 0x8d);
      si = (uint16_t)(si + 1);
      if ((int16_t)si >= 0x10) break;
    } while (!(rd8(es, (uint16_t)(bx + 1)) & 2));
    W(-2) = si;
  }
  if ((int16_t)W(-2) >= 0x10) goto Ld92;                      /* 0bb6 */
  {
    uint16_t pbase = (uint16_t)((uint32_t)owner_of(W(-0x1a), W(-0x1c)) * 0xc0u);
    ax = drd16((uint16_t)(pbase + 0x5a68));
    ax = (uint16_t)(ax & 0x0fff);
    if (ax >= 0x200) goto Ld92;
  }
  ax = rand_n(3);
  si = (ax == 0) ? 2 : 1;
  if (g_current_turn > 100 && !(g_game_flags & 4)) {
    ax = (uint16_t)(g_current_turn / 100);
    ax = (uint16_t)(ax + 1);
    ax = rand_n(ax);
    si = (uint16_t)(si + ax);
  }
  W(-0xc) = si;
  if (!((int16_t)si <= 5)) W(-0xc) = 5;
  if ((int16_t)W(-4) > 0) {
    ax = (uint16_t)(W(-0xc) + 1);
    ax = rand_n(ax);
    W(-0xc) = (uint16_t)(W(-0xc) + ax);
  }
  r = CALL_ORIG(7, 0x1f2e, FAR_CDECL, 2, W(-0x12), rd16(W(-0x1a), (uint16_t)(W(-0x1c) + 6)));
  W(-6) = FP_OFF(r);
  W(-4) = FP_SEG(r);
  if ((W(-4) | W(-6)) == 0) goto Ld92;
  r = CALL_ORIG(7, 0x1bca, FAR_CDECL, 3, W(-0x1c), W(-0x1a), W(-0xc));
  ax = (uint16_t)r;
  rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(W(-0x1a), W(-0x1c)), 0x14f,
            (uint16_t)(rd16(W(-4), W(-6)) | 0x8000), ax, W(-0xc)}, 5);
  ax = rd16(W(-0x1a), W(-0x1c));
  if (!((int16_t)rd16(W(-4), W(-6)) >= (int16_t)ax)) W(-0x22) = (uint16_t)(W(-0x22) + 1);
  wr32(W(-4), (uint16_t)(W(-6) + 8), rd32(W(-0x1a), (uint16_t)(W(-0x1c) + 8)));
  {
    uint32_t eax = rd32(W(-0x1a), (uint16_t)(W(-0x1c) + 8));
    uint16_t nseg = W(-4), noff = W(-6);
    uint16_t wsi = rd16(nseg, (uint16_t)(noff + 0x64));
    uint16_t wes = rd16(nseg, (uint16_t)(noff + 0x66));
    wr32(wes, (uint16_t)(wsi + 4), eax);
    wr8(nseg, (uint16_t)(noff + 5), (uint8_t)(rd8(nseg, (uint16_t)(noff + 5)) | 0x20));
  }
  bx = (uint16_t)(W(-0x12) << 2);                             /* 0cc6 */
  ax = drd16((uint16_t)(bx + 0xbe));
  dx = drd16((uint16_t)(bx + 0xc0));
  cx = (uint16_t)((uint32_t)W(-2) * 0x8du);
  ax = (uint16_t)(ax + cx);
  W(-0xa) = ax;
  W(-8) = dx;
  es = dx;
  bx = ax;
  if (rd8(es, (uint16_t)(bx + 0x7c)) & 2) {
    for (uint16_t i = 0; i < 0x8d; i++)
      wr8(es, (uint16_t)(W(-0xa) + i), rd8(rt_ss, (uint16_t)(rt_bp - 0xc2 + i)));
    bx = W(-0xa);
    cx = W(-2);
    ax = (uint16_t)((uint16_t)rd8(es, (uint16_t)(bx + 0x7c)) << 8);
    cx = (uint16_t)(cx << 10);
    ax = (uint16_t)(ax ^ (uint16_t)(cx & 0xff00));
    ax = (uint16_t)(ax & 0x7c00);
    wr16(es, (uint16_t)(bx + 0x7b), (uint16_t)(rd16(es, (uint16_t)(bx + 0x7b)) ^ ax));
    wr8(es, (uint16_t)(bx + 0x7c), (uint8_t)(rd8(es, (uint16_t)(bx + 0x7c)) | 0x80));
    wr32(es, (uint16_t)(bx + 0x7f), 0);
    wr32(es, (uint16_t)(bx + 0x83), 0);
    si = W(-0xc6);
    dwr8((uint16_t)(si + 0x5a65), (uint8_t)(drd8((uint16_t)(si + 0x5a65)) + 1));
    ax = owner_of(W(-0x1a), W(-0x1c));
    g_current_player = ax;
    CALL_ORIG(7, 0x2dbe, FAR_CDECL, 2, W(-0xa), W(-8));
    g_current_player = 0xffff;
  }
  {                                                           /* 0d50 */
    uint32_t add = (uint32_t)(int32_t)(int16_t)W(-0xc);
    bx = W(-0xa);
    es = W(-8);
    wr32(es, (uint16_t)(bx + 0x7f), rd32(es, (uint16_t)(bx + 0x7f)) + add);
    wr32(es, (uint16_t)(bx + 0x83), rd32(es, (uint16_t)(bx + 0x83)) + add);
  }
  ax = W(-0xc);
  si = W(-2);
  si = (uint16_t)(si + si);
  wr16(W(-4), (uint16_t)(W(-6) + si + 0xc), ax);
  r = CALL_ORIG(10, 0x292a, FAR_CDECL, 3, W(-6), W(-4), 1);
  wr16(W(-4), (uint16_t)(W(-6) + 0x5c), (uint16_t)r);
  wr16(W(-4), (uint16_t)(W(-6) + 0x5e), (uint16_t)(r >> 16));
  goto Ldbd;
Ld92:
  r = CALL_ORIG(7, 0x1bca, FAR_CDECL, 3, W(-0x1c), W(-0x1a), 0);
  rt_call_far(MK_FP(SEG_SEL(6), 0x7328), FAR_CDECL, (const uint16_t[]){owner_of(W(-0x1a), W(-0x1c)), 0x0150, 0xffff, (uint16_t)r, 0}, 5);
Ldbd:
  di = W(-0x1c);
  goto fleet_next;

Ldfa:                                                         /* 0dfa: no flag, the levels pay */
  {
    int32_t e = (int32_t)D(-0x16) - 0x1388;
    e = e / 0x4b0;
    D(-0x10) = (uint32_t)(e + 6);
  }
  si = 0;
  W(-2) = si;
  es = W(-0x1a);
  ax = (uint16_t)((uint32_t)rd16(es, (uint16_t)(di + 2)) * 0xc0u);
  W(-0xc4) = ax;
  W(-4) = si;
  di = si;
  do {
    bx = W(-0xc4);
    ax = (uint16_t)(int16_t)lvl((uint16_t)(bx + si + 0x5a7e));
    di = (uint16_t)(di + ax);
    si = (uint16_t)(si + 1);
  } while ((int16_t)si < 6);
  if ((int32_t)D(-0x10) > 0xa) D(-0x10) = 0xa;
  if ((int16_t)di >= 0x6c) {
    W(-0x10) = 1;
    W(-0xe) = 0;
  } else if ((int16_t)di >= 0x60) {
    W(-0x10) = 2;
    W(-0xe) = 0;
  } else if ((int16_t)di >= 0x54) {
    D(-0x10) = D(-0x10) - 3;
  } else if ((int16_t)di >= 0x48) {
    D(-0x10) = D(-0x10) - 2;
  } else if ((int16_t)di >= 0x3c) {
    D(-0x10) = D(-0x10) - 1;
  }
  si = W(-0x1c);                                              /* 0e93 */
  r = CALL_ORIG(7, 0x1bca, FAR_CDECL, 8, si, W(-0x1a), W(-0x10), 0, 0, 0, 0, 0);
  ax = (uint16_t)r;
  ax = (uint16_t)(W(-6) & 0x1fff);
  di = (ax != 0x1fff) ? 0x109 : 0x10a;
  rt_call_far(MK_FP(SEG_SEL(6), 0x7344), FAR_CDECL, (const uint16_t[]){owner_of(W(-0x1a), si), di, 0xffff, (uint16_t)r, W(-0x10), 0, 0, 0, 0, 0}, 10);
  for (;;) {                                                  /* 0eda */
    uint32_t old = D(-0x10);
    D(-0x10) = D(-0x10) - 1;
    if (old == 0) goto Ldbd;
    di = W(-0x12);
    ax = rand_n(4);
    if ((int16_t)ax < 3) {
      di = W(-0x12);
      bx = (uint16_t)((uint32_t)di * 0xc0u);
      W(-0xc6) = bx;
      ax = rand_n(6);
      bx = W(-0xc6);
      si = ax;
      W(-4) = ax;
      ax = (uint16_t)(int16_t)lvl((uint16_t)(bx + si + 0x5a7e));
      if ((int16_t)ax < (int16_t)W(-0x18)) goto Lf69;
    }
    W(-4) = 0;                                                /* 0f23 */
    si = 1;
    ax = (uint16_t)((uint32_t)di * 0xc0u);
    W(-0xc6) = ax;
    do {
      bx = (uint16_t)(W(-0xc6) + W(-4));
      uint8_t al = drd8((uint16_t)(bx + 0x5a7e));
      bx = W(-0xc6);
      if ((int8_t)al > lvl((uint16_t)(bx + si + 0x5a7e))) W(-4) = si;
      si = (uint16_t)(si + 1);
    } while ((int16_t)si < 6);
    si = (uint16_t)((uint32_t)W(-0x12) * 0xc0u);
    W(-0xc6) = si;
    bx = W(-4);
    ax = (uint16_t)(int16_t)lvl((uint16_t)(bx + si + 0x5a7e));
    if ((int16_t)ax >= (int16_t)W(-0x18)) goto Ldbd;
  Lf69:
    bx = W(-0xc6);
    for (uint16_t i = 0; i < 6; i++)
      wr8(rt_ss, (uint16_t)(rt_bp - 0x2c + i), drd8((uint16_t)(bx + 0x5a7e + i)));
    si = W(-4);
    wr8(rt_ss, (uint16_t)(rt_bp + si - 0x2c), (uint8_t)(rd8(rt_ss, (uint16_t)(rt_bp + si - 0x2c)) + 1));
    ax = g_current_player;
    W(-2) = ax;
    ax = W(-0x12);
    g_current_player = ax;
    r = CALL_ORIG(27, 0x49ee, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x2c), rt_ss);
    W(-0xc) = (uint16_t)r;
    W(-0xa) = (uint16_t)(r >> 16);
    ax = W(-2);
    g_current_player = ax;
    ax = (uint16_t)((uint32_t)W(-0x12) * 0x30u);
    si = (uint16_t)(si + ax);
    si = (uint16_t)(si << 2);
    si = (uint16_t)(si + 0x5a84);
    W(-0xc8) = si;
    {
      uint32_t eax = drd32(si);
      eax = eax + eax;
      D(-8) = eax;
    }
    if (!((int32_t)D(-0xc) <= 0)) D(-8) = D(-8) + D(-0xc);
    ax = W(-8);
    dx = W(-6);
    dwr16(si, ax);
    dwr16((uint16_t)(si + 2), dx);
    CALL_ORIG(23, 0x4c50, FAR_CDECL, 1, 0);
  }
}
