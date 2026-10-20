/* Decompiled code segment 6 (NE segment 6, selector 0x0030).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg06.c; Ghidra's pseudo-C in re/decomp/seg06 is only a reading aid.
 *
 * Segment 6 is the message window of the game (MESSAGEWNDPROC: the turn messages, their buttons and the
 * comment text) and the message store behind it, plus the message dialog MSGDLG:
 *   the message buffer: the far block DS:0xaac/0xaae, DS:0xab0 bytes used, DS:0xab4 messages in it, DS:0xab6
 *   the shown one (0xffff none), DS:0xaba "only the starred ones", DS:0xac4 the number of comments in the list
 *   DS:0xabc (far pointer chain), DS:0xac0 the list of the player's own comments (DS:0xab8 the current one,
 *   DS:0xac6 their count). Two record layouts share the buffer: during a turn (6:0x7344 / 0x739e) a
 *   record is 5 bytes of header (byte 0: low nibble the player, high nibble the length of the values; word
 *   +1: bits 0-8 the message id, bits 9-15 one "word" flag per value; word +3 the subject) followed by the
 *   values; in the player's own file (6:0x752e, 6:0x8460) the header is 4 bytes (word 0 the id and flags,
 *   word +2 the subject). The number of values of message id n is the signed byte cs:0xe74 + n (a table
 *   in this code segment, valid for ids 0 .. 0x180).
 *   DS:0x534e (49 bytes): one bit per message id, "this kind occurred"; DS:0x5508 (49 bytes): one bit
 *   per id, "the player filters it" (6:0x8960).
 * The functions:
 *   6:0x0000  MESSAGEWNDPROC, the window procedure of the message window
 *   6:0x6a68  update_message_controls: bring the window's buttons, combo box, edit controls and title in step
 *   6:0x6ef4  next_message_index / 6:0x6f66 prev_message_index: the next / previous message to show
 *   6:0x6fc6  draw_message_icons: the small pictures of the title line (the shown message's, the filter
 *             button, the right end)
 *   6:0x7294  hit_test_message_window: the zone (0 .. 3) of the title line under a point
 *   6:0x7328  add_message_two_values: add_message with two values
 *   6:0x7344  add_message / 6:0x739e add_message_front: a 5-byte-header record at the end / the front
 *   6:0x7410  build_message_record: build a 5-byte-header record
 *   6:0x752e  add_player_message: a 4-byte-header record at the front or the end
 *   6:0x7646  message_id: the id of message idx
 *   6:0x7668  read_message: copy message idx (id, subject, values) out of the buffer
 *   6:0x774a  message_text: the text of message idx
 *   6:0x7778  format_message: expand a message format with its argument words into DS:0x553e
 *   6:0x7ed0  MSGDLG, the number dialog of the "goto" button
 *   6:0x80c8  format_message_id / 6:0x80e0 format_string_id: format_message on the format of a message id /
 *             of a string id
 *   6:0x80fa  delete_messages / 6:0x8154 has_message: mark deleted / look for the 5-byte-header records of a
 *             player, id and subject
 *   6:0x81b4  visit_message_planets: 14:0x51cc on the planet of each of a player's messages with id 7, 0x23,
 *             0x40 or 0x8f
 *   6:0x829a  mark_comment_authors: set bits in the player rows of the authors of the comments a player
 *             receives
 *   6:0x8308  write_player_messages: write a player's messages and the comments the player receives
 *   6:0x841c  clear_message_store: empty the message store
 *   6:0x8460  load_player_messages: read the messages and comments from the turn file
 *   6:0x8640  store_edited_comment: store the comment being edited into the list and move by dir
 *   6:0x8864  unpack_message_format: unpack the format string of a message id into DS:0x5276
 *   6:0x8960  set_message_filter: set the filter bit of a message id and of the ids that go with it
 * Names: the export names MESSAGEWNDPROC and MSGDLG are kept; Ghidra's other names are all generic
 * (FUN_1028_<off>), so the C names describe what each function does (CONVENTIONS rule f). Two are
 * placeholders: visit_message_planets (what 14:0x51cc does with the planet, the player and 3 is not known
 * yet; it is not decompiled) and mark_comment_authors (what the bits of the player rows DS:0x5a6a /
 * DS:0x5a6b mean is not known yet). The "player" of add_player_message and load_player_messages is the
 * player's own file (4-byte-header records); write_player_messages writes one player's 5-byte-header
 * records in that form (without byte 0). The old fn_06_<off> names are kept as aliases in the comment
 * above each function.
 *
 * Every function that calls anything mirrors its original frame (CONVENTIONS rule d): FRAME_ENTER_REGS(n,
 * pushes) with n the ENTER size (or 0 after PUSH BP; MOV BP,SP) and one push for each PUSH SI / PUSH DI.
 * Leaves that never expose a stack address use ordinary C locals. Near calls inside the segment are
 * PUSH CS; CALL near, i.e. far calls with a 4-byte return address; a callee that mirrors a frame is
 * reached through rt_call_far. The callees in other segments that are not decompiled yet: CALL_ORIG
 * (CDECL: the arguments in declaration order, i.e. the last one pushed first). */
#include <string.h>

#include "runtime/fp80.h"
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
static inline uint32_t dsfp(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }
/* A byte of this code segment (the tables at cs:0xe74, 0xff5, 0x103d, 0x104b, 0x11cc). */
static inline uint8_t cs8(uint16_t off) { return rd8(SEG_SEL(6), off); }
/* The number of values of message id `id` (MOV AL,CS:[BX+0xE74]; CBW). */
static inline int16_t nvals(uint16_t id) { return (int16_t)(int8_t)cs8((uint16_t)(id + 0x0e74)); }

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
/* A far pointer to the stack local at BP + o (SS = DGROUP at every call), and its near offset. */
static inline uint32_t bpfp(int16_t o) { return MK_FP(rt_ss, (uint16_t)(rt_bp + o)); }
static inline uint16_t bpoff(int16_t o) { return (uint16_t)(rt_bp + o); }
/* PUSH word / dword on the emulated stack (the variable arguments of _wsprintf and the like). */
__attribute__((unused)) static inline void pushw(uint16_t v) {
  rt_sp = (uint16_t)(rt_sp - 2);
  wr16(rt_ss, rt_sp, v);
}
__attribute__((unused)) static inline void pushd(uint32_t v) {
  pushw((uint16_t)(v >> 16));
  pushw((uint16_t)v);
}
__attribute__((unused)) static inline void popw(uint16_t n) { rt_sp = (uint16_t)(rt_sp + 2 * n); }
/* A by-name call that can run guest code (a window or dialog procedure): the original has pushed the
 * call's argument bytes and the 4-byte far return address, so the procedure runs that many bytes lower
 * than the C frame's SP; rule d. */
#define API_SP(argbytes, expr)                                  \
  ({                                                            \
    uint16_t api_sp_ = rt_sp;                                   \
    rt_sp = (uint16_t)(api_sp_ - (uint16_t)(argbytes) - 4u);    \
    __typeof__(expr) api_r_ = (expr);                           \
    rt_sp = api_sp_;                                            \
    api_r_;                                                     \
  })
/* The call (seg 6, off) of a function of this segment with nw argument words (PUSH CS; CALL near). */
static inline uint32_t call6(uint16_t off, const uint16_t *a, uint16_t nw) {
  return rt_call_far(MK_FP(SEG_SEL(6), off), FAR_CDECL, a, nw);
}
static inline uint16_t call6_1(uint16_t off, uint16_t a0) {
  const uint16_t a[1] = {a0};
  return (uint16_t)call6(off, a, 1);
}
/* 34:0x0c98, the C runtime's far memory copy (dst, src, count), not decompiled. */
static inline void fmemcpy(uint32_t dst, uint32_t src, uint16_t n) {
  (void)CALL_ORIG(34, 0x0c98, FAR_CDECL, 5, (uint16_t)dst, (uint16_t)(dst >> 16), (uint16_t)src,
                  (uint16_t)(src >> 16), n);
}
/* Bit (v & 7) of the byte DS:0x5508 + (v SAR 3): the filter bit of a message id. */
static inline uint16_t filter_bit(uint16_t v) {
  uint16_t bx = (uint16_t)(int16_t)((int16_t)v >> 3);
  return (uint16_t)(drd8((uint16_t)(bx + 0x5508)) & (uint8_t)(1u << (v & 7)));
}

/* read_message: 6:0x7668 (Ghidra FUN_1028_7668, alias fn_06_7668).
 * 226 bytes. ENTER 0x12,0; PUSH DI; PUSH SI. A leaf (it calls nothing;
 * its frame is never exposed): ordinary C locals. FAR CDECL (idx [BP+6], out [BP+8], a near pointer).
 * Walks the 4-byte-header records of the message buffer (DS:0xaac/0xaae, DS:0xab0 bytes) to record
 * number idx and copies it to DS:out: +0 the id (word & 0x1ff), +2 the subject word, +4.. one word per
 * value (a byte value zero-extended). Returns 0 when idx < 0 or idx >= DS:0xab4 (signed), else 1 (also
 * when the buffer ends before record idx; out is then not written). The index argument is counted down
 * in its stack slot (DEC [BP+6]). AX only. */
DECOMP(6, 0x7668, read_message, FAR_CDECL)
uint16_t read_message(int16_t idx, uint16_t out)
{
  if (idx < 0) return 0;
  if ((int16_t)drd16(0x0ab4) <= idx) return 0;
  uint16_t seg = drd16(0x0aae);
  uint16_t si = drd16(0x0aac);
  uint16_t end = (uint16_t)(si + drd16(0x0ab0));
  for (;;) {
    if (si >= end) break;
    uint16_t cx = si;
    uint16_t flags = (uint16_t)(rd16(seg, si) >> 9);
    si = (uint16_t)(si + 4);
    uint16_t di = out;
    if (idx == 0) {
      dwr16(di, (uint16_t)(rd16(seg, cx) & 0x01ff));
      dwr16((uint16_t)(di + 2), rd16(seg, (uint16_t)(cx + 2)));
    }
    int16_t n = nvals((uint16_t)(rd16(seg, cx) & 0x01ff));
    if (n > 0) {
      uint16_t p = (uint16_t)(di + 4);
      uint16_t k = (uint16_t)n;
      uint16_t f = flags;
      do {
        if (idx == 0) {
          uint16_t v = (f & 1) ? rd16(seg, si) : rd8(seg, si);
          dwr16(p, v);
        }
        si = (uint16_t)(si + ((f & 1) ? 2 : 1));
        f = (uint16_t)(f >> 1);
        p = (uint16_t)(p + 2);
        k = (uint16_t)(k - 1);
      } while (k != 0);
    }
    int16_t was = idx;
    idx = (int16_t)(uint16_t)(idx - 1);
    if (!(was > 0)) break;
  }
  return 1;
}

/* message_id: 6:0x7646 (Ghidra FUN_1028_7646, alias fn_06_7646).
 * 33 bytes. ENTER 0x12,0. FAR CDECL (idx). The id of message idx: 6:0x7668
 * into the local record at BP-0x12; 0xffff when it fails, else the record's first word. AX only. */
DECOMP(6, 0x7646, message_id, FAR_CDECL)
uint16_t message_id(uint16_t idx)
{
  FRAME_ENTER(0x12);
  const uint16_t a[2] = {idx, bpoff(-0x12)};
  uint16_t r = (uint16_t)call6(0x7668, a, 2) == 0 ? 0xffff : L16(-0x12);
  FRAME_LEAVE();
  return r;
}

/* next_message_index: 6:0x6ef4 (Ghidra FUN_1028_6ef4, alias fn_06_6ef4).
 * 113 bytes. PUSH BP; MOV BP,SP; PUSH SI. FAR CDECL (flag). The index of
 * the next message after DS:0xab6: when DS:0xaba ("starred only") is set and flag is 0, simply DS:0xab6 + 1
 * if that is below DS:0xab4 + DS:0xac4 (the messages and the comments), else 0xffff. Otherwise the first
 * message after DS:0xab6 whose filter bit (6:0x7646, DS:0x5508) is "flag == 0 ? clear : set"... precisely:
 * the loop stops at the first one whose (bit == 0) differs from flag. Past the messages (DS:0xab4) the
 * comments count as found: the index itself when it is below DS:0xab4 + DS:0xac4, else 0xffff. Compares
 * are signed. AX only. */
DECOMP(6, 0x6ef4, next_message_index, FAR_CDECL)
uint16_t next_message_index(uint16_t flag)
{
  FRAME_ENTER_REGS(0, 1);
  uint16_t si = drd16(0x0ab6), r;
  if (drd16(0x0aba) != 0 && flag == 0) {
    uint16_t ax = (uint16_t)(drd16(0x0ab4) + drd16(0x0ac4) - 1);
    r = ((int16_t)ax <= (int16_t)si) ? 0xffff : (uint16_t)(si + 1);
    FRAME_LEAVE();
    return r;
  }
  for (;;) {
    si = (uint16_t)(si + 1);
    if ((int16_t)si >= (int16_t)drd16(0x0ab4)) {
      r = ((int16_t)(uint16_t)(drd16(0x0ab4) + drd16(0x0ac4)) > (int16_t)si) ? si : 0xffff;
      break;
    }
    uint16_t v = call6_1(0x7646, si);
    uint16_t z = filter_bit(v) == 0 ? 1 : 0;
    if ((z ^ flag) != 0) {
      r = si;
      break;
    }
  }
  FRAME_LEAVE();
  return r;
}

/* prev_message_index: 6:0x6f66 (Ghidra FUN_1028_6f66, alias fn_06_6f66).
 * 96 bytes. PUSH BP; MOV BP,SP; PUSH SI. FAR CDECL (flag). The index of the
 * previous message before DS:0xab6, the mirror of 6:0x6ef4: with DS:0xaba set and flag 0, DS:0xab6 - 1 if
 * DS:0xab6 > 0, else 0xffff; when DS:0xab6 is past the messages (DS:0xab4 < DS:0xab6, signed) DS:0xab6 - 1;
 * otherwise the last message before it whose (filter bit == 0) differs from flag, 0xffff when none. */
DECOMP(6, 0x6f66, prev_message_index, FAR_CDECL)
uint16_t prev_message_index(uint16_t flag)
{
  FRAME_ENTER_REGS(0, 1);
  uint16_t si = drd16(0x0ab6), r;
  if (drd16(0x0aba) != 0 && flag == 0) {
    r = ((int16_t)si <= 0) ? 0xffff : (uint16_t)(si - 1);
    FRAME_LEAVE();
    return r;
  }
  if ((int16_t)drd16(0x0ab4) < (int16_t)si) {
    FRAME_LEAVE();
    return (uint16_t)(si - 1);
  }
  for (;;) {
    si = (uint16_t)(si - 1);
    if ((int16_t)si < 0) {
      r = 0xffff;
      break;
    }
    uint16_t v = call6_1(0x7646, si);
    uint16_t z = filter_bit(v) == 0 ? 1 : 0;
    if ((z ^ flag) != 0) {
      r = si;
      break;
    }
  }
  FRAME_LEAVE();
  return r;
}

/* hit_test_message_window: 6:0x7294 (Ghidra FUN_1028_7294, alias fn_06_7294).
 * 147 bytes. ENTER 2,0; PUSH SI. FAR CDECL (x [BP+6], y [BP+8]: a POINT
 * passed by value). The hit test of the message window: 0 outside the rectangle DS:0x4b64 (PtInRect);
 * 1 left of 0x4b64 + (0x4b6a - 0x4b66) (the text area) when a message is shown (0 <= DS:0xab6 < DS:0xab4)
 * and bit 0 of DS:0x791 (the comment mode) is clear; 2 left of 0x4b68 - (0x4b6a - 0x4b66) when bit 0 of
 * DS:0x791 is clear and some id has both its "occurred" (DS:0x534e) and "filtered" (DS:0x5508) bit; 3 at
 * the right end (from that border - 0x18 on) when bit 2 of DS:0x80 is clear; else 0. Signed compares. */
DECOMP(6, 0x7294, hit_test_message_window, FAR_CDECL)
uint16_t hit_test_message_window(int16_t x, int16_t y)
{
  FRAME_ENTER_REGS(2, 1);
  uint16_t r = 0;
  uint32_t pt = ((uint32_t)(uint16_t)y << 16) | (uint16_t)x;
  if (PtInRect(dsfp(0x4b64), pt) == 0) goto out;
  {
    uint16_t ax = (uint16_t)(drd16(0x4b6a) - drd16(0x4b66) + drd16(0x4b64));
    if ((int16_t)ax > x && (int16_t)drd16(0x0ab6) >= 0 && (int16_t)drd16(0x0ab4) > (int16_t)drd16(0x0ab6) &&
        (drd8(0x0791) & 1) == 0) {
      r = 1;
      goto out;
    }
  }
  {
    uint16_t ax = (uint16_t)(drd16(0x4b66) - drd16(0x4b6a) + drd16(0x4b68));
    if (!((int16_t)ax > x) && (drd8(0x0791) & 1) == 0) {
      uint16_t si = 0;
      while (si < 0x31 && (drd8((uint16_t)(si + 0x534e)) & drd8((uint16_t)(si + 0x5508))) == 0) si++;
      r = si == 0x31 ? 0 : 2;
      goto out;
    }
    if ((drd8(0x0080) & 4) != 0) goto out;
    ax = (uint16_t)(ax - 0x18);
    if ((int16_t)ax > x) goto out;
    r = 3;
  }
out:
  FRAME_LEAVE();
  return r;
}

/* build_message_record: 6:0x7410 (Ghidra FUN_1028_7410, alias fn_06_7410).
 * 286 bytes. ENTER 0x14,0; PUSH DI; PUSH SI. FAR CDECL (out [BP+6], a near
 * pointer; player [BP+8]; id [BP+0xa]; subject [BP+0xc]; then the values, one word each from [BP+0xe] up,
 * as many as the table cs:0xe74 + id says). Builds a 5-byte-header record at DS:out (see the top of the file):
 * byte 0 low nibble = player, high nibble = the length of the values; word +1 = id (bits 0-8; bits 9-15
 * cleared, then bit 9 + k set for each value k whose high byte is not 0: such a value is stored as a word,
 * the others as a byte); word +3 = subject. Returns the record length, 0 when player is 0xffff or the
 * player's row (DS:0x5a6a + 0xc0 * player) has bit 9 set and its bits 13-15 are not all set and the id is
 * none of 0x8f, 7, 0x23, 0x40 (a player that does not get messages), 0xffff when the buffer is nearly full
 * (DS:0xab0 + 0x14 > 0xffc8, unsigned). The bytes of out above the low nibble of byte 0 and bit 8 of word +1
 * are only partly written: byte 0's high nibble at the end, word +1's bit 8 kept from the id. The values are
 * read from the caller's stack (DS:[BP+0xe+2k]); the frame is mirrored only so that this reads the same
 * words as the original also for an id whose count exceeds the arguments the caller pushed. AX only. */
DECOMP(6, 0x7410, build_message_record, FAR_CDECL)
uint16_t build_message_record(uint16_t out, uint16_t player, uint16_t id, uint16_t subject)
{
  FRAME_ENTER_REGS(0x14, 2);
  uint16_t r;
  if (player == 0xffff) {
    r = 0;
    goto done;
  }
  {
    uint16_t w = drd16((uint16_t)((uint16_t)(player * 0xc0) + 0x5a6a));
    if ((w & 0x0200) != 0 && ((w >> 8) & 0xe0) != 0xe0) {
      if (id != 0x8f && id != 7 && id != 0x23 && id != 0x40) {
        r = 0;
        goto done;
      }
    }
  }
  if ((uint16_t)(drd16(0x0ab0) + 0x14) > 0xffc8) {
    r = 0xffff;
    goto done;
  }
  {
    uint16_t di = out;
    dwr8(di, (uint8_t)(drd8(di) ^ ((drd8(di) ^ (uint8_t)player) & 0x0f)));
    {
      uint16_t ax = (uint16_t)(drd16((uint16_t)(di + 1)) ^ id);
      ax = (uint16_t)(ax & 0x01ff);
      dwr16((uint16_t)(di + 1), (uint16_t)(drd16((uint16_t)(di + 1)) ^ ax));
    }
    dwr8((uint16_t)(di + 2), (uint8_t)(drd8((uint16_t)(di + 2)) & 1));
    dwr16((uint16_t)(di + 3), subject);
    uint16_t start = (uint16_t)(di + 5);
    uint16_t p = start;
    if (nvals(id) > 0) {
      uint16_t src = bpoff(0x0e);
      uint16_t bit = 1;
      int16_t k = 0;
      do {
        if (rd8(rt_ss, (uint16_t)(src + 1)) != 0) {
          dwr16((uint16_t)(out + 1), (uint16_t)(drd16((uint16_t)(out + 1)) | (uint16_t)(bit << 9)));
          dwr16(p, rd16(rt_ss, src));
          p = (uint16_t)(p + 2);
        } else {
          dwr8(p, rd8(rt_ss, src));
          p = (uint16_t)(p + 1);
        }
        src = (uint16_t)(src + 2);
        bit = (uint16_t)(bit << 1);
        k = (int16_t)(uint16_t)(k + 1);
      } while (nvals(id) > k);
    }
    {
      uint8_t al = (uint8_t)((uint8_t)((uint8_t)p - (uint8_t)start) << 4);
      uint8_t cl = al;
      al = (uint8_t)((al ^ drd8(out)) & 0x0f);
      dwr8(out, (uint8_t)(cl ^ al));
    }
    r = (uint16_t)(p - out);
  }
done:
  FRAME_LEAVE();
  return r;
}

/* add_message: 6:0x7344 (Ghidra FUN_1028_7344, alias fn_06_7344).
 * 90 bytes. ENTER 0x28,0; PUSH SI. FAR CDECL with ten argument words
 * (player, id, subject, then seven values). Appends a message: 6:0x7410 builds it in the local buffer at
 * BP-0x28 from the same ten words; when its length si is positive the record is copied to the end of the
 * buffer (34:0x0c98(DS:0xaac + DS:0xab0, local, si)), DS:0xab0 grows by si, DS:0xab4 by 1, and the result
 * is 1; otherwise 1 when si is 0 and 0 when it is negative (the buffer is full). AX only. */
DECOMP(6, 0x7344, add_message, FAR_CDECL)
uint16_t add_message(uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5, uint16_t a6,
                    uint16_t a7, uint16_t a8, uint16_t a9)
{
  FRAME_ENTER_REGS(0x28, 1);
  const uint16_t a[11] = {bpoff(-0x28), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
  uint16_t si = (uint16_t)call6(0x7410, a, 11), r;
  if ((int16_t)si <= 0) {
    r = si == 0 ? 1 : 0;
  } else {
    fmemcpy(MK_FP(drd16(0x0aae), (uint16_t)(drd16(0x0aac) + drd16(0x0ab0))), bpfp(-0x28), si);
    dwr16(0x0ab0, (uint16_t)(drd16(0x0ab0) + si));
    dwr16(0x0ab4, (uint16_t)(drd16(0x0ab4) + 1));
    r = 1;
  }
  FRAME_LEAVE();
  return r;
}

/* add_message_two_values: 6:0x7328 (Ghidra FUN_1028_7328, alias fn_06_7328).
 * 28 bytes. PUSH BP; MOV BP,SP. FAR CDECL (player, id, subject, v0, v1): a
 * message with two values, 6:0x7344(player, id, subject, v0, v1, 0, 0, 0, 0, 0). AX is its result. */
DECOMP(6, 0x7328, add_message_two_values, FAR_CDECL)
uint16_t add_message_two_values(uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4)
{
  FRAME_ENTER(0);
  const uint16_t a[10] = {a0, a1, a2, a3, a4, 0, 0, 0, 0, 0};
  uint16_t r = (uint16_t)call6(0x7344, a, 10);
  FRAME_LEAVE();
  return r;
}

/* add_message_front: 6:0x739e (Ghidra FUN_1028_739e, alias fn_06_739e).
 * 113 bytes. ENTER 0x28,0; PUSH SI. FAR CDECL, ten words as 6:0x7344, but
 * the record is put in FRONT of the buffer: the old contents move up by its length si (34:0x0c98(DS:0xaac
 * + si, DS:0xaac, DS:0xab0)), then the record is copied to DS:0xaac. Same counters and result. */
DECOMP(6, 0x739e, add_message_front, FAR_CDECL)
uint16_t add_message_front(uint16_t a0, uint16_t a1, uint16_t a2, uint16_t a3, uint16_t a4, uint16_t a5, uint16_t a6,
                    uint16_t a7, uint16_t a8, uint16_t a9)
{
  FRAME_ENTER_REGS(0x28, 1);
  const uint16_t a[11] = {bpoff(-0x28), a0, a1, a2, a3, a4, a5, a6, a7, a8, a9};
  uint16_t si = (uint16_t)call6(0x7410, a, 11), r;
  if ((int16_t)si <= 0) {
    r = si == 0 ? 1 : 0;
  } else {
    uint16_t seg = drd16(0x0aae), off = drd16(0x0aac);
    fmemcpy(MK_FP(seg, (uint16_t)(off + si)), MK_FP(seg, off), drd16(0x0ab0));
    fmemcpy(drd32(0x0aac), bpfp(-0x28), si);
    dwr16(0x0ab0, (uint16_t)(drd16(0x0ab0) + si));
    dwr16(0x0ab4, (uint16_t)(drd16(0x0ab4) + 1));
    r = 1;
  }
  FRAME_LEAVE();
  return r;
}

/* add_player_message: 6:0x752e (Ghidra FUN_1028_752e, alias fn_06_752e).
 * 279 bytes. ENTER 0x4a,0; PUSH DI; PUSH SI. FAR CDECL (front [BP+6], id
 * [BP+8], subject [BP+0xa], then the values from [BP+0xc] up, as many as cs:0xe74 + id says). Adds a
 * 4-byte-header record (the player's own file): returns 0 when DS:0xab0 + 0x14 > 0xffc8 (unsigned). Sets
 * the "occurred" bit of the id (DS:0x534e + (id SAR 3), bit id & 7), builds the record at BP-0x4a (word 0 =
 * id with the word flags in bits 9-15 as in 6:0x7410, word +2 = subject, then the values), and puts it at
 * the front (front != 0: the buffer moves up first) or at the end of the buffer; DS:0xab0 and DS:0xab4
 * grow, DS:0xab6 becomes 6:0x6ef4(0) (from 0xffff). Returns 1. The values are read from the caller's stack
 * like 6:0x7410's. */
DECOMP(6, 0x752e, add_player_message, FAR_CDECL)
uint16_t add_player_message(uint16_t front, uint16_t id, uint16_t subject)
{
  FRAME_ENTER_REGS(0x4a, 2);
  if ((uint16_t)(drd16(0x0ab0) + 0x14) > 0xffc8) {
    FRAME_LEAVE();
    return 0;
  }
  {
    /* MOV AX,[BP-0x4A]; XOR AX,DI; AND AH,1; XOR [SI],AX: bits 0-8 become the id */
    uint16_t ax = (uint16_t)((L16(-0x4a) ^ id) & 0x01ff);
    L16(-0x4a) = (uint16_t)(L16(-0x4a) ^ ax);
  }
  {
    uint16_t bx = (uint16_t)(int16_t)((int16_t)id >> 3);
    dwr8((uint16_t)(bx + 0x534e), (uint8_t)(drd8((uint16_t)(bx + 0x534e)) | (uint8_t)(1u << (id & 7))));
  }
  L8(-0x49) = (uint8_t)(L8(-0x49) & 1);
  L16(-0x48) = subject;
  uint16_t si = bpoff(-0x46);
  if (nvals(id) > 0) {
    uint16_t di = bpoff(0x0c);
    uint16_t bit = 1;
    int16_t k = 0;
    do {
      if (rd8(rt_ss, (uint16_t)(di + 1)) != 0) {
        L16(-0x4a) = (uint16_t)(L16(-0x4a) | (uint16_t)(bit << 9));
        wr16(rt_ss, si, rd16(rt_ss, di));
        si = (uint16_t)(si + 2);
      } else {
        wr8(rt_ss, si, rd8(rt_ss, di));
        si = (uint16_t)(si + 1);
      }
      di = (uint16_t)(di + 2);
      bit = (uint16_t)(bit << 1);
      k = (int16_t)(uint16_t)(k + 1);
    } while (nvals(id) > k);
  }
  uint16_t len = (uint16_t)(si - bpoff(-0x4a));
  L16(-2) = len;
  if (front != 0) {
    uint16_t seg = drd16(0x0aae), off = drd16(0x0aac);
    fmemcpy(MK_FP(seg, (uint16_t)(off + len)), MK_FP(seg, off), drd16(0x0ab0));
    fmemcpy(drd32(0x0aac), bpfp(-0x4a), L16(-2));
  } else {
    fmemcpy(MK_FP(drd16(0x0aae), (uint16_t)(drd16(0x0aac) + drd16(0x0ab0))), bpfp(-0x4a), len);
  }
  dwr16(0x0ab0, (uint16_t)(drd16(0x0ab0) + L16(-2)));
  dwr16(0x0ab4, (uint16_t)(drd16(0x0ab4) + 1));
  dwr16(0x0ab6, 0xffff);
  dwr16(0x0ab6, call6_1(0x6ef4, 0));
  FRAME_LEAVE();
  return 1;
}

/* delete_messages: 6:0x80fa (Ghidra FUN_1028_80fa, alias fn_06_80fa).
 * 90 bytes. ENTER 8,0; PUSH DI; PUSH SI. A leaf: ordinary C locals. FAR
 * CDECL (player, id, subject). Over the 5-byte-header records of the buffer: every record of the player
 * (byte 0 low nibble) with that id (word +1 & 0x1ff) and subject (word +3) gets id 0x1ff (OR word +1,
 * 0x1ff: deleted). Returns how many. Record length (byte 0 >> 4) + 5; unsigned compares against the end. */
DECOMP(6, 0x80fa, delete_messages, FAR_CDECL)
uint16_t delete_messages(uint16_t player, uint16_t id, uint16_t subject)
{
  uint16_t cx = 0;
  uint16_t seg = drd16(0x0aae), si = drd16(0x0aac);
  uint16_t end = (uint16_t)(si + drd16(0x0ab0));
  if (end <= si) return 0;
  do {
    if ((uint16_t)(rd8(seg, si) & 0x0f) == player && (uint16_t)(rd16(seg, (uint16_t)(si + 1)) & 0x01ff) == id &&
        rd16(seg, (uint16_t)(si + 3)) == subject) {
      cx = (uint16_t)(cx + 1);
      wr16(seg, (uint16_t)(si + 1), (uint16_t)(rd16(seg, (uint16_t)(si + 1)) | 0x01ff));
    }
    si = (uint16_t)(si + (uint16_t)(rd8(seg, si) >> 4) + 5);
  } while (si < end);
  return cx;
}

/* has_message: 6:0x8154 (Ghidra FUN_1028_8154, alias fn_06_8154).
 * 96 bytes. ENTER 8,0; PUSH DI; PUSH SI. A leaf. FAR CDECL (player, id,
 * subject). 1 when the buffer has a 5-byte-header record of the player with that id and subject, else 0.
 * No caller was found (the inventory lists none); tested anyway. */
DECOMP(6, 0x8154, has_message, FAR_CDECL)
uint16_t has_message(uint16_t player, uint16_t id, uint16_t subject)
{
  uint16_t seg = drd16(0x0aae), si = drd16(0x0aac);
  uint16_t end = (uint16_t)(si + drd16(0x0ab0));
  if (end <= si) return 0;
  do {
    if ((uint16_t)(rd8(seg, si) & 0x0f) == player && (uint16_t)(rd16(seg, (uint16_t)(si + 1)) & 0x01ff) == id &&
        rd16(seg, (uint16_t)(si + 3)) == subject)
      return 1;
    si = (uint16_t)(si + (uint16_t)(rd8(seg, si) >> 4) + 5);
  } while (si < end);
  return 0;
}

/* visit_message_planets (PLACEHOLDER NAME, see the file header): 6:0x81b4 (Ghidra FUN_1028_81b4, alias fn_06_81b4).
 * 229 bytes. ENTER 0x10,0; PUSH DI; PUSH SI. FAR CDECL (player). Returns
 * nothing (AX is left over). For every 5-byte-header record of the player whose id is 7, 0x23 or 0x40 (the
 * planet is the subject word +3) or 0x8f (the planet is the second value: a word when flag bit 1 is set,
 * else a byte; it follows the first value, one or two bytes by flag bit 0), the planet 7:0x01e4(planet) is
 * looked up and, when found, 14:0x51cc(planet far pointer, player, 3) is called. The other ids go through
 * a switch whose remaining cases all do nothing. */
DECOMP(6, 0x81b4, visit_message_planets, FAR_CDECL)
void visit_message_planets(uint16_t player)
{
  FRAME_ENTER_REGS(0x10, 2);
  uint16_t si = drd16(0x0aac);
  L16(-8) = drd16(0x0aae);
  uint16_t end = (uint16_t)(si + drd16(0x0ab0));
  if (end <= si) goto done;
  L16(-0xe) = end;
  do {
    uint16_t seg = L16(-8);
    if ((uint16_t)(rd8(seg, si) & 0x0f) == player) {
      uint16_t id = (uint16_t)(rd16(seg, (uint16_t)(si + 1)) & 0x01ff);
      uint16_t planet;
      bool hit = false;
      if (id == 0x40 || id == 7 || id == 0x23) {
        planet = rd16(seg, (uint16_t)(si + 3));
        hit = true;
      } else if (id == 0x8f) {
        uint16_t fl = (uint16_t)(rd16(seg, (uint16_t)(si + 1)) >> 9);
        L16(-0x10) = fl;
        uint16_t p = (uint16_t)(((fl & 1) == 1 ? 1 : 0) + si + 6);
        planet = (L8(-0x10) & 2) != 0 ? rd16(seg, p) : rd8(seg, p);
        hit = true;
      }
      if (hit) {
        uint32_t r = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, planet);
        L16(-2) = (uint16_t)(r >> 16);
        if (r != 0) (void)CALL_ORIG(14, 0x51cc, FAR_CDECL, 4, (uint16_t)r, (uint16_t)(r >> 16), player, 3);
      }
    }
    seg = L16(-8);
    si = (uint16_t)(si + (uint16_t)(rd8(seg, si) >> 4) + 5);
  } while (si < L16(-0xe));
done:
  FRAME_LEAVE();
}

/* mark_comment_authors (PLACEHOLDER NAME, see the file header): 6:0x829a (Ghidra FUN_1028_829a, alias fn_06_829a).
 * 109 bytes. ENTER 4,0; PUSH DI; PUSH SI. A leaf. FAR CDECL (player).
 * Returns nothing (AX is 0 on every path but player == 0xffff, where it is the caller's AX). Over the
 * comment list DS:0xac0 (far pointer chain: +0 next, +4 a player, +6 a word): a node whose +6 is 0 and +4
 * is not the player, or whose +6 is player + 1 and the row of player +4 does not have bit 0 of byte
 * 0x5a6b + 0xc0 * (+4) set, sets that bit and then bits 0 and 1 (clearing bit 2) of the row byte 0x5a6a. */
DECOMP(6, 0x829a, mark_comment_authors, FAR_CDECL)
void mark_comment_authors(uint16_t player)
{
  if (player == 0xffff) return;
  uint16_t bx = drd16(0x0ac0), seg = drd16(0x0ac2);
  if ((seg | bx) == 0) return;
  for (;;) {
    uint16_t w6 = rd16(seg, (uint16_t)(bx + 6)), w4 = rd16(seg, (uint16_t)(bx + 4));
    bool mark;
    if (w6 == 0 && w4 != player) {
      mark = true;
    } else if ((uint16_t)(w6 - player - 1) != 0) {
      mark = false;
    } else {
      mark = (drd8((uint16_t)((uint16_t)(w4 * 0xc0) + 0x5a6b)) & 1) == 0;
    }
    if (mark) {
      uint16_t si = (uint16_t)(w4 * 0xc0);
      dwr8((uint16_t)(si + 0x5a6b), (uint8_t)(drd8((uint16_t)(si + 0x5a6b)) | 1));
      si = (uint16_t)(si + 0x5a6a);
      uint16_t ax = drd16(si);
      ax = (uint16_t)((ax & 0xff00) | (uint8_t)(((uint8_t)ax & 0xfb) | 3));
      dwr16(si, ax);
    }
    uint32_t nx = rd32(seg, bx);
    bx = (uint16_t)nx;
    seg = (uint16_t)(nx >> 16);
    if ((seg | bx) == 0) break;
  }
}

/* write_player_messages: 6:0x8308 (Ghidra FUN_1028_8308, alias fn_06_8308).
 * 275 bytes. ENTER 0x408,0; PUSH DI; PUSH SI. FAR CDECL (player). Writes the
 * player's messages and comments to the turn file: the player's 5-byte-header records that are not deleted
 * (id 0x1ff) are gathered without their first byte (length (byte 0 >> 4) + 4) into the 1024-byte local
 * buffer at BP-0x408, which is written with 14:0x579a(0xc, length, buffer) whenever another record could
 * overflow it (di + 0x14 >= 0x400, signed) and at the end when not empty. Then each comment node of
 * DS:0xac0 whose +6 is player + 1, or whose +6 is 0 and +4 is not the player, is written with
 * 14:0x579a(0x28, abs(+0xa) + 0xc, node). Nothing for player 0xffff. */
DECOMP(6, 0x8308, write_player_messages, FAR_CDECL)
void write_player_messages(uint16_t player)
{
  FRAME_ENTER_REGS(0x408, 2);
  uint16_t di = 0;
  if (player == 0xffff) goto done;
  {
    uint16_t si = drd16(0x0aac);
    L16(-2) = drd16(0x0aae);
    uint16_t end = (uint16_t)(si + drd16(0x0ab0));
    L16(-6) = L16(-2);
    if (end > si) {
      L16(-8) = end;
      do {
        if ((int16_t)(uint16_t)(di + 0x14) >= 0x400) {
          (void)CALL_ORIG(14, 0x579a, FAR_CDECL, 4, 0x0c, di, bpoff(-0x408), rt_ss);
          di = 0;
        }
        uint16_t seg = L16(-2);
        if ((uint16_t)(rd8(seg, si) & 0x0f) == player && (uint16_t)(rd16(seg, (uint16_t)(si + 1)) & 0x01ff) != 0x01ff) {
          uint16_t n = (uint16_t)((uint16_t)(rd8(seg, si) >> 4) + 4);
          fmemcpy(MK_FP(rt_ss, (uint16_t)(bpoff(-0x408) + di)), MK_FP(seg, (uint16_t)(si + 1)), n);
          seg = L16(-2);
          di = (uint16_t)(di + (uint16_t)(rd8(seg, si) >> 4) + 4);
        }
        si = (uint16_t)(si + (uint16_t)(rd8(seg, si) >> 4) + 5);
      } while (si < L16(-8));
    }
  }
  if (di != 0) (void)CALL_ORIG(14, 0x579a, FAR_CDECL, 4, 0x0c, di, bpoff(-0x408), rt_ss);
  {
    uint16_t si = drd16(0x0ac0);
    L16(-2) = drd16(0x0ac2);
    if ((L16(-2) | si) == 0) goto done;
    di = player;
    do {
      uint16_t seg = L16(-2);
      uint16_t w6 = rd16(seg, (uint16_t)(si + 6));
      bool wr;
      if (w6 == 0 && rd16(seg, (uint16_t)(si + 4)) != di)
        wr = true;
      else
        wr = (uint16_t)(w6 - di - 1) == 0;
      if (wr) {
        int16_t v = (int16_t)rd16(seg, (uint16_t)(si + 0x0a));
        uint16_t a = (uint16_t)(v < 0 ? (uint16_t)(0u - (uint16_t)v) : (uint16_t)v);
        (void)CALL_ORIG(14, 0x579a, FAR_CDECL, 4, 0x28, (uint16_t)(a + 0x0c), si, seg);
      }
      seg = L16(-2);
      uint32_t nx = rd32(seg, si);
      si = (uint16_t)nx;
      L16(-2) = (uint16_t)(nx >> 16);
    } while ((L16(-2) | si) != 0);
  }
done:
  FRAME_LEAVE();
}

/* clear_message_store: 6:0x841c (Ghidra FUN_1028_841c, alias fn_06_841c).
 * 68 bytes. PUSH DI (no frame). A leaf. FAR CDECL, no argument. Empties the
 * message store: DS:0xab6 = 0xffff; the 49 bytes of DS:0x534e and of DS:0x5508 (REP STOSD x12 + STOSB, REP
 * STOSW x24 + STOSB) are cleared; the dwords DS:0xabc and DS:0xac0 and the words DS:0xab0, 0xab4, 0xab8, 0xac4,
 * 0xac6 become 0. AX = 0 (left by XOR EAX,EAX). */
DECOMP(6, 0x841c, clear_message_store, FAR_CDECL)
uint16_t clear_message_store(void)
{
  dwr16(0x0ab6, 0xffff);
  for (uint16_t k = 0; k < 49; k++) dwr8((uint16_t)(0x534e + k), 0);
  for (uint16_t k = 0; k < 49; k++) dwr8((uint16_t)(0x5508 + k), 0);
  dwr32(0x0abc, 0);
  dwr32(0x0ac0, 0);
  dwr16(0x0ab0, 0);
  dwr16(0x0ab4, 0);
  dwr16(0x0ab8, 0);
  dwr16(0x0ac4, 0);
  dwr16(0x0ac6, 0);
  return 0;
}

/* unpack_message_format: 6:0x8864 (Ghidra FUN_1028_8864, alias fn_06_8864).
 * 251 bytes. ENTER 0x14,0; PUSH DI; PUSH SI. A leaf. FAR CDECL (id). The
 * format string of message id, unpacked into DS:0x5276; returns 0x5276. When the signed byte DS:0xbb4 equals
 * id the buffer is returned as it is. The strings are packed in this code segment: group g = id SAR 6, entry
 * e = id & 0x3f; cs:0x104b + 64 g holds the lengths (in nibbles) of the group's entries, cs:0x103d + 2 g the
 * start of the group's nibbles (+ 0x11cc). The entry starts at nibble d = the sum of the earlier lengths
 * (byte cs:[0x11cc + start + d SAR 1], high nibble first when d is even). Each nibble n selects the
 * character cs:0xff5 + acc + n, except n = 15, which adds 15 to acc for the next nibble; acc is 0 again
 * after a character. AX only. */
DECOMP(6, 0x8864, unpack_message_format, FAR_CDECL)
uint16_t unpack_message_format(uint16_t id)
{
  if ((int16_t)(int8_t)drd8(0x0bb4) == (int16_t)id) return 0x5276;
  uint16_t bx = (uint16_t)(id & 0x3f);
  uint16_t g = (uint16_t)(int16_t)((int16_t)id >> 6);
  uint16_t base = (uint16_t)(rd16(SEG_SEL(6), (uint16_t)(0x103d + (uint16_t)(g + g))) + 0x11cc);
  uint16_t si = (uint16_t)((uint16_t)(g << 6) + 0x104b);
  uint16_t d = 0;
  if ((int16_t)bx > 0) {
    uint16_t cx = bx;
    do {
      d = (uint16_t)(d + cs8(si));
      si = (uint16_t)(si + 1);
      cx = (uint16_t)(cx - 1);
    } while (cx != 0);
  }
  base = (uint16_t)(base + (uint16_t)(int16_t)((int16_t)d >> 1));
  uint16_t count = cs8(si);
  uint16_t hi = (d & 1) == 0 ? 1 : 0;
  uint16_t outp = 0x5276;
  uint16_t acc = 0;
  uint16_t p = base;
  for (;;) {
    uint16_t c = count;
    count = (uint16_t)(count - 1);
    if (c == 0) break;
    uint16_t n;
    if (hi != 0) {
      n = (uint16_t)(cs8(p) >> 4);
    } else {
      n = (uint16_t)(cs8(p) & 0x0f);
      p = (uint16_t)(p + 1);
    }
    hi = hi == 0 ? 1 : 0;
    acc = (uint16_t)(acc + n);
    if (n == 0x0f) continue;
    dwr8(outp, cs8((uint16_t)(acc + 0x0ff5)));
    outp = (uint16_t)(outp + 1);
    acc = 0;
  }
  dwr8(outp, 0);
  return 0x5276;
}

/* Sets bit (n & 7) of the byte DS:0x5508 + (n SAR 3) to the value v (0 or 1). */
static void set_filter(uint16_t n, uint8_t v)
{
  uint8_t cl = (uint8_t)(n & 7);
  uint16_t bx = (uint16_t)((uint16_t)(int16_t)((int16_t)n >> 3) + 0x5508);
  dwr8(bx, (uint8_t)((uint8_t)(v << cl) | (drd8(bx) & (uint8_t)~(1u << cl))));
}
/* set_message_filter: 6:0x8960 (Ghidra FUN_1028_8960, alias fn_06_8960).
 * 601 bytes. ENTER 0x10,0; PUSH DI; PUSH SI. A leaf. FAR CDECL (id, on).
 * Sets the filter bit of message id (DS:0x5508, bit id & 7 of byte id SAR 3) to (on != 0), then keeps the
 * related ids in step: 0x35/0x36/0x39/0x3a/0x79/0x7a set the bit of id ^ 3 too, 0x37/0x38 the bit
 * (~id & 7) of byte (id ^ 8) SAR 3, 0x2f/0x30 the bit (~id & 7) of byte (id ^ 0x18) SAR 3, 0x42 .. 0x4d the
 * bit of id ^ 1; the ranges 0x2b-0x2e, 0x60-0x64, 0x6a-0x6e and 0x91-0xa8 get the value on all their ids.
 * Returns nothing. */
DECOMP(6, 0x8960, set_message_filter, FAR_CDECL)
void set_message_filter(uint16_t id, uint16_t on)
{
  uint8_t v = on != 0 ? 1 : 0;
  set_filter(id, v);
  uint16_t lo, hi;
  switch (id) {
    case 0x35: case 0x36: case 0x39: case 0x3a: case 0x79: case 0x7a:
      set_filter((uint16_t)((id & 0xfff8) | ((id ^ 3) & 7)), v);
      return;
    case 0x37: case 0x38: {
      uint16_t bx = (uint16_t)((uint16_t)(int16_t)((int16_t)(id ^ 8) >> 3) + 0x5508);
      uint8_t cl = (uint8_t)(~id & 7);
      dwr8(bx, (uint8_t)((uint8_t)(v << cl) | (drd8(bx) & (uint8_t)~(1u << cl))));
      return;
    }
    case 0x2f: case 0x30: {
      uint16_t bx = (uint16_t)((uint16_t)(int16_t)((int16_t)(id ^ 0x18) >> 3) + 0x5508);
      uint8_t cl = (uint8_t)(~id & 7);
      dwr8(bx, (uint8_t)((uint8_t)(v << cl) | (drd8(bx) & (uint8_t)~(1u << cl))));
      return;
    }
    default: break;
  }
  if ((int16_t)id >= 0x2b && (int16_t)id <= 0x2e) {
    lo = 0x2b;
    hi = 0x2e;
  } else if ((int16_t)id >= 0x42 && (int16_t)id <= 0x4d) {
    set_filter((uint16_t)((id & 0xfff8) | ((id ^ 1) & 7)), v);
    return;
  } else if ((int16_t)id >= 0x60 && (int16_t)id <= 0x64) {
    lo = 0x60;
    hi = 0x64;
  } else if ((int16_t)id >= 0x6a && (int16_t)id <= 0x6e) {
    lo = 0x6a;
    hi = 0x6e;
  } else if ((int16_t)id >= 0x91 && (int16_t)id <= 0xa8) {
    lo = 0x91;
    hi = 0xa8;
  } else {
    return;
  }
  for (uint16_t s = lo; (int16_t)s <= (int16_t)hi; s++) set_filter(s, v);
}

/* load_player_messages: 6:0x8460 (Ghidra FUN_1028_8460, alias fn_06_8460).
 * 480 bytes. ENTER 0x2a,0; PUSH DI; PUSH SI. FAR CDECL, no argument. Loads
 * the player's messages and comments from the turn file, block by block: the current block's header word is
 * DS:0x2910 (type in bits 10-15, length in bits 0-9), its data at DS:0x4db2, and 14:0x305a reads the next
 * one. While the type is 12 (high byte & 0xfc = 0x30) the data is appended to the message buffer (when it
 * is not empty and 0xffc8 - length > DS:0xab0 + the bytes taken so far, unsigned); then DS:0xab0 grows by
 * what was taken and the new 4-byte-header records are counted (DS:0xab4) and their "occurred" bits set
 * (DS:0x534e). Then a catch point is set (34:0x0800, a setjmp, into BP-0x28, registered in DS:0x6e: the
 * old DS:0x6e is put back only if a file error throws to it, and the loop then only reads on) and while the
 * type is 40 (0xa0) each block becomes a comment node of the list DS:0xabc: 12:0x0232(length, heap 8), the
 * block copied in, its next pointer (the first dword) cleared, appended at the end of the chain, DS:0xac4
 * counts them. Finally DS:0xab6 = 6:0x6ef4(0) (from 0xffff), which is also AX. */
DECOMP(6, 0x8460, load_player_messages, FAR_CDECL)
uint16_t load_player_messages(void)
{
  FRAME_ENTER_REGS(0x2a, 2);
  uint16_t bx = 0, di, si;
  L16(-0x16) = 0;
  di = (uint16_t)(drd16(0x0aac) + drd16(0x0ab0));
  L16(-0xc) = drd16(0x0aae);
  while ((drd8(0x2911) & 0xfc) == 0x30) {
    uint16_t len = (uint16_t)(drd16(0x2910) & 0x03ff);
    L16(-0x2a) = len;
    if (len != 0 && (uint16_t)(0xffc8 - len) > (uint16_t)(drd16(0x0ab0) + bx)) {
      L16(-0xe) = di;
      L16(-2) = bx;
      fmemcpy(MK_FP(L16(-0xc), (uint16_t)(di + bx)), dsfp(0x4db2), L16(-0x2a));
      bx = (uint16_t)(L16(-2) + (drd16(0x2910) & 0x03ff));
      di = L16(-0xe);
    }
    L16(-0xe) = di;
    L16(-2) = bx;
    (void)CALL_ORIG(14, 0x305a, FAR_CDECL, 0);
    bx = L16(-2);
    di = L16(-0xe);
  }
  dwr16(0x0ab0, (uint16_t)(drd16(0x0ab0) + bx));
  {
    uint16_t end = (uint16_t)(di + bx);
    uint16_t seg = L16(-0xc);
    L16(-0x14) = end;
    L16(-0x12) = seg;
    L16(-2) = bx;
    if (end > di) {
      do {
        L16(-0xa) = di;
        L16(-8) = seg;
        uint8_t cl = (uint8_t)(rd8(seg, di) & 7);
        uint16_t b = (uint16_t)((rd16(seg, di) & 0x01f8) >> 3);
        dwr8((uint16_t)(b + 0x534e), (uint8_t)(drd8((uint16_t)(b + 0x534e)) | (uint8_t)(1u << cl)));
        dwr16(0x0ab4, (uint16_t)(drd16(0x0ab4) + 1));
        L16(-2) = (uint16_t)(rd16(seg, di) >> 9);
        di = (uint16_t)(di + 4);
        int16_t n = nvals((uint16_t)(rd16(seg, L16(-0xa)) & 0x01ff));
        L16(-0x10) = (uint16_t)n;
        L16(-6) = 0;
        if (n > 0) {
          L16(-4) = (uint16_t)n;
          si = L16(-2);
          do {
            di = (uint16_t)(di + ((si & 1) == 1 ? 1 : 0) + 1);
            si = (uint16_t)(si >> 1);
            L16(-6) = (uint16_t)(L16(-6) + 1);
            L16(-4) = (uint16_t)(L16(-4) - 1);
          } while (L16(-4) != 0);
          L16(-2) = si;
        }
      } while (di < L16(-0x14));
    }
  }
  L16(-0xe) = di;
  {
    /* the last "next" slot of the comment chain DS:0xabc */
    uint16_t es = MEM_SEL_DGROUP, sb = 0x0abc;
    if (rd32(es, sb) != 0) {
      do {
        uint32_t nx = rd32(es, sb);
        sb = (uint16_t)nx;
        es = (uint16_t)(nx >> 16);
      } while (rd32(es, sb) != 0);
    }
    uint16_t dx = drd16(0x006e);
    dwr16(0x006e, bpoff(-0x28));
    L16(-4) = es;
    L16(-6) = sb;
    L16(-2) = dx;
  }
  if ((uint16_t)CALL_ORIG(34, 0x0800, FAR_CDECL, 1, bpoff(-0x28)) != 0) {
    dwr16(0x006e, L16(-2));
    L16(-0x16) = 1;
    goto L8619;
  }
  while ((drd8(0x2911) & 0xfc) == 0xa0) {
    if (L16(-0x16) == 0) {
      const uint16_t a[2] = {(uint16_t)(drd16(0x2910) & 0x03ff), 8};
      uint32_t p = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, a, 2);
      wr32(L16(-4), L16(-6), p);
      L32(-6) = rd32(L16(-4), L16(-6));
      uint16_t cx = (uint16_t)(drd16(0x2910) & 0x03ff);
      for (uint16_t k = 0; k < cx; k++) wr8(L16(-4), (uint16_t)(L16(-6) + k), drd8((uint16_t)(0x4db2 + k)));
      wr32((uint16_t)(p >> 16), (uint16_t)p, 0);
      dwr16(0x0ac4, (uint16_t)(drd16(0x0ac4) + 1));
    }
  L8619:
    (void)CALL_ORIG(14, 0x305a, FAR_CDECL, 0);
  }
  dwr16(0x0ab6, 0xffff);
  uint16_t r = call6_1(0x6ef4, 0);
  dwr16(0x0ab6, r);
  FRAME_LEAVE();
  return r;
}

/* abs() of a 16-bit word as CWD; XOR AX,DX; SUB AX,DX computes it (-32768 stays). */
static inline uint16_t abs16(uint16_t v) { return (int16_t)v < 0 ? (uint16_t)(0u - v) : v; }

/* store_edited_comment: 6:0x8640 (Ghidra FUN_1028_8640, alias fn_06_8640).
 * 547 bytes. ENTER 0x12,0; PUSH DI; PUSH SI. FAR CDECL (dir: -1, 0, 1 or
 * 0x3e8). Stores the comment being edited (the window DS:0x59e6, text buffer DS:0x53ea) into the list
 * DS:0xac0 at position DS:0xab8 and moves by dir. The node at that position (its slot: the "next" field
 * before it) is the one replaced. dir 0x3e8 deletes it (dir becomes 0, no text read); otherwise the text is
 * GetWindowText(DS:0x59e6, DS:0x53ea, 0x3e8). An empty text removes the node (9:0x641c(1) "changed",
 * unlinked, 12:0x04d0 frees it, DS:0xac6 and DS:0xab8 count down) and then dir -1 / 1 moves DS:0xab8 within
 * [0, DS:0xac6]; returns 0. A text is packed by 8:0xa57e into DS:0x53ea + 0x400 (length +0xa > 0) or kept
 * as it is (length +0xa = ~length, negative) and stored: an existing node is re-allocated (12:0x03ec; 9:0x641c
 * only when the length, the subject word +6 or the bytes differ), else a new node (12:0x0232, heap 8, next
 * 0, +8 = DS:0xab6, DS:0xac6 + 1; 9:0x641c). The node gets +4 = DS:0x14c, +6 = SendMessage(DS:0x53e0,
 * 0x407, 0, 0) (the combo box selection), +0xa the length and the bytes at +0xc; DS:0xab8 += dir (0 when it
 * becomes negative). Returns 1. */
DECOMP(6, 0x8640, store_edited_comment, FAR_CDECL)
uint16_t store_edited_comment(uint16_t dir)
{
  FRAME_ENTER_REGS(0x12, 2);
  uint16_t si, di, r;
  {
    uint16_t es = MEM_SEL_DGROUP, sb = 0x0ac0;
    uint16_t cx = drd16(0x0ab8);
    for (;;) {
      uint16_t ax = cx;
      cx = (uint16_t)(cx - 1);
      if (!((int16_t)ax > 0)) break;
      uint32_t nx = rd32(es, sb);
      sb = (uint16_t)nx;
      es = (uint16_t)(nx >> 16);
    }
    L16(-2) = es;
    L16(-4) = sb;
    si = rd16(es, sb);
    L16(-6) = rd16(es, (uint16_t)(sb + 2));
  }
  if (dir == 0x03e8) {
    di = 0;
    dir = 0;
  } else {
    di = (uint16_t)GetWindowText(drd16(0x59e6), drd32(0x53ea), 0x03e8);
  }
  if (di == 0) {
    if ((L16(-6) | si) != 0) {
      (void)CALL_ORIG(9, 0x641c, FAR_CDECL, 1, 1);
      wr32(L16(-2), L16(-4), rd32(L16(-6), si));
      const uint16_t a[3] = {si, L16(-6), 8};
      (void)rt_call_far(MK_FP(SEG_SEL(12), 0x04d0), FAR_CDECL, a, 3);
      dwr16(0x0ac6, (uint16_t)(drd16(0x0ac6) - 1));
      if ((int16_t)drd16(0x0ab8) > (int16_t)di) dwr16(0x0ab8, (uint16_t)(drd16(0x0ab8) - 1));
    }
    if (dir == 0xffff) {
      if ((int16_t)drd16(0x0ab8) > (int16_t)di) dwr16(0x0ab8, (uint16_t)(drd16(0x0ab8) - 1));
    } else if (dir == 1) {
      if ((int16_t)drd16(0x0ab8) < (int16_t)drd16(0x0ac6)) dwr16(0x0ab8, (uint16_t)(drd16(0x0ab8) + 1));
    }
    r = 0;
    goto done;
  }
  L16(-8) = si;
  L16(-0x12) = di;
  {
    uint16_t lo = drd16(0x53ea), hi = drd16(0x53ec);
    uint16_t packed = (uint16_t)CALL_ORIG(8, 0xa57e, FAR_CDECL, 5, lo, hi, (uint16_t)(lo + 0x400), hi, bpoff(-0x12));
    if (packed != 0) {
      L16(-0xc) = L16(-0x12);
      L16(-0x10) = (uint16_t)(drd16(0x53ea) + 0x400);
      L16(-0xe) = drd16(0x53ec);
    } else {
      L16(-0xc) = di;
      L16(-0xc) = (uint16_t)(0xffff - di);
      L16(-0x10) = drd16(0x53ea);
      L16(-0xe) = drd16(0x53ec);
    }
  }
  L16(-0x12) = (uint16_t)(abs16(L16(-0xc)) + 0x0c);
  L16(-0xa) = (uint16_t)API_SP(10, SendMessage(drd16(0x53e0), 0x0407, 0, 0));
  if (L32(-8) != 0) {
    uint16_t nseg = L16(-6), noff = L16(-8);
    bool same = false;
    if (rd16(nseg, (uint16_t)(noff + 0x0a)) == L16(-0xc) && rd16(nseg, (uint16_t)(noff + 6)) == L16(-0xa)) {
      /* REPE CMPSW / CMPSB over L16(-0xc) bytes: the node's text against the new one */
      uint16_t n = L16(-0xc);
      uint16_t a = (uint16_t)(noff + 0x0c), b = L16(-0x10);
      uint16_t bseg = L16(-0xe);
      same = true;
      for (uint16_t w = 0; w < (uint16_t)(n >> 1); w++) {
        if (rd16(nseg, (uint16_t)(a + 2 * w)) != rd16(bseg, (uint16_t)(b + 2 * w))) {
          same = false;
          break;
        }
      }
      if (same && (n & 1) != 0 && rd8(nseg, (uint16_t)(a + (n & 0xfffe))) != rd8(bseg, (uint16_t)(b + (n & 0xfffe))))
        same = false;
    }
    if (!same) (void)CALL_ORIG(9, 0x641c, FAR_CDECL, 1, 1);
    const uint16_t a[4] = {L16(-8), L16(-6), L16(-0x12), 8};
    uint32_t p = rt_call_far(MK_FP(SEG_SEL(12), 0x03ec), FAR_CDECL, a, 4);
    L16(-8) = (uint16_t)p;
    L16(-6) = (uint16_t)(p >> 16);
  } else {
    (void)CALL_ORIG(9, 0x641c, FAR_CDECL, 1, 1);
    const uint16_t a[2] = {L16(-0x12), 8};
    uint32_t p = rt_call_far(MK_FP(SEG_SEL(12), 0x0232), FAR_CDECL, a, 2);
    L16(-8) = (uint16_t)p;
    L16(-6) = (uint16_t)(p >> 16);
    wr32((uint16_t)(p >> 16), (uint16_t)p, 0);
    dwr16(0x0ac6, (uint16_t)(drd16(0x0ac6) + 1));
    wr16((uint16_t)(p >> 16), (uint16_t)((uint16_t)p + 8), drd16(0x0ab6));
  }
  {
    uint16_t nseg = L16(-6), noff = L16(-8);
    wr32(L16(-2), L16(-4), L32(-8));
    wr16(nseg, (uint16_t)(noff + 4), drd16(0x014c));
    wr16(nseg, (uint16_t)(noff + 6), L16(-0xa));
    wr16(nseg, (uint16_t)(noff + 0x0a), L16(-0xc));
    fmemcpy(MK_FP(nseg, (uint16_t)(noff + 0x0c)), MK_FP(L16(-0xe), L16(-0x10)), abs16(L16(-0xc)));
  }
  dwr16(0x0ab8, (uint16_t)(drd16(0x0ab8) + dir));
  if ((int16_t)drd16(0x0ab8) < 0) dwr16(0x0ab8, 0);
  r = 1;
done:
  FRAME_LEAVE();
  return r;
}

/* DGROUP string helpers in the way the original's REPNE SCASB / REP MOVSW sequences work (DS = SS = ES =
 * DGROUP). */
static uint16_t ds_strlen(uint16_t p)
{
  uint16_t n = 0;
  while (drd8((uint16_t)(p + n)) != 0) n = (uint16_t)(n + 1);
  return n;
}
/* strcpy(dst, src) including the NUL; returns strlen(src). */
static uint16_t ds_strcpy(uint16_t dst, uint16_t src)
{
  uint16_t n = (uint16_t)(ds_strlen(src) + 1);
  for (uint16_t k = 0; k < n; k++) dwr8((uint16_t)(dst + k), drd8((uint16_t)(src + k)));
  return (uint16_t)(n - 1);
}
/* strcat(dst, src). */
static void ds_strcat(uint16_t dst, uint16_t src)
{
  uint16_t n = (uint16_t)(ds_strlen(src) + 1);
  uint16_t d = (uint16_t)(dst + ds_strlen(dst));
  for (uint16_t k = 0; k < n; k++) dwr8((uint16_t)(d + k), drd8((uint16_t)(src + k)));
}
/* strcpy(DS:dst, seg:src) including the NUL; returns strlen(src). */
static uint16_t far_strcpy(uint16_t dst, uint16_t seg, uint16_t src)
{
  uint16_t n = 0;
  while (rd8(seg, (uint16_t)(src + n)) != 0) n = (uint16_t)(n + 1);
  for (uint16_t k = 0; k <= n; k++) dwr8((uint16_t)(dst + k), rd8(seg, (uint16_t)(src + k)));
  return n;
}

/* format_message: 6:0x7778 (Ghidra FUN_1028_7778, alias fn_06_7778;
 * the inventory's size 73 is only the part before the jump table: the
 * function runs to 0x7ecf, 1880 bytes, with a 54-word jump table at cs:0x77c2). ENTER 0x1f4,0; PUSH DI;
 * PUSH SI. FAR CDECL (fmt [BP+6], a near string; args [BP+8], a far pointer to the argument words). Expands
 * a message format into DS:0x553e and returns 0x553e (AX). A plain character is copied; '\' followed by a
 * letter from 'E' to 'z' is a conversion (other characters after '\' are copied as they are). Most
 * conversions take one argument word and append a string:
 *   E  9:0x3e72(high byte, low byte)      F  7:0x1c00(w)         G  8:0x8dc0(w + 0x4d3, out): loaded in place
 *   I  w ? DS:0xb8a : DS:0xb94             M  the word DS:0x4c2 + 2w (a near string)    e  DS:0x48e + 2w
 *   L, l  7:0x0b84(w & 0xf, fmt is 'L', w & 0x10, w & 0x20, (w & 0xc0) >> 6, 0) (the flags as 0/1)
 *   O  7:0x0b84((w >> 9) & 0xf, 0 ...)       S  "the" (DS:0xba4, 5 bytes) + 7:0x0b84(w) + DS:0xba9, nothing for
 *      the current player DS:0x14c        P  w / 100 with _wsprintf(DS:0x178) when w / 100 >= the double
 *      DS:0x1ff8 (FILD, FCOM m64, JB), else w / 100 and w % 100 with _wsprintf(DS:0x18a)
 *   U, V, v  a dword with _wsprintf(DS:0x170) (two words); then, except for 'v', the plural ending
 *      DS:0xb66 + 2 * DS-local "count" (BP-0x10; 'V' takes it from the next word, which stays unread)
 *   X  nothing (the word is skipped)       Z  a set of player bits: one name (7:0x0b84(bit, 0, 1, 1, 0, 0)) or
 *      the list "a, b and c" (", " and DS:0xb9e between names; players below DS:0x78); 0 takes no word
 *   f, h, r, t, y  the game name DS:0x5764, then f "%d"-like DS:0xb72 with DS:0x14c + 1 (t when it is not
 *      0xffff: DS:0xb77; both fall back to h), h ".hst"-like DS:0xb7c, r DS:0xb81, y DS:0xb86 (h and y take no
 *      word)
 *   i  _wsprintf(DS:0x16a, w)               j  8:0x8df4(w + 0x54)    u  _wsprintf(DS:0xbb1, w)
 *   k  two words (kind, index byte) as a request for 2:0x50f8; the name at the record's table pointer + 8
 *   m  the word DS:0x464 + 2w, and w becomes the count of BP-0x10
 *   n  0xfffe w: 7:0x1902(w); 0xffff w: 'o' on w; else 7:0x25ce(0, 0xffff, w0, w1) (two words)
 *   g  7:0x1902(w)    o  the high bit of w: 's', else 7:0x1cce(w)    p  7:0x1cce(w)    s  7:0x1a70(w | 0x8000)
 *   w  the text DS:0x5866 (no word)       z  a design (w >> 5 the player, w & 0x1f the slot, of the tables
 *      DS:0xbe / DS:0x10c, 0x8d bytes each), preceded by the owner's name and a space when not the current
 *      player.
 * The locals live in the mirrored frame (BP-0x10 .. BP-2 and the buffer at BP-0x1f0): some conversions read
 * what an earlier one left there ('k' reads the high byte of BP-6, and the table pointer at BP-4 / BP-2 when
 * 2:0x50f8 does not set it). */
#define F_OUT L16(-0xe)
DECOMP(6, 0x7778, format_message, FAR_CDECL)
uint16_t format_message(uint16_t fmt, uint32_t args)
{
  FRAME_ENTER_REGS(0x1f4, 2);
  uint16_t aseg = (uint16_t)(args >> 16), aoff = (uint16_t)args;
#define ARG(k) rd16(aseg, (uint16_t)(aoff + (k)))
  L16(-0x10) = 0xffff;
  F_OUT = 0x553e;
  if (drd8(fmt) == 0) goto L7ec3;
  for (;;) {
    uint16_t ax;
    if (drd8(fmt) != 0x5c) goto L7797;
    fmt = (uint16_t)(fmt + 1);
    {
      uint8_t c = drd8(fmt);
      uint16_t sel = (uint16_t)((uint16_t)(int16_t)(int8_t)c - 0x45);
      if (sel > 0x35) goto L7797;
      switch (c) {
        case 'E':
          ax = (uint16_t)CALL_ORIG(9, 0x3e72, FAR_CDECL, 2, rd8(aseg, (uint16_t)(aoff + 1)), rd8(aseg, aoff));
          goto L7e84;
        case 'F':
          ax = (uint16_t)CALL_ORIG(7, 0x1c00, FAR_CDECL, 1, ARG(0));
          goto L7e84;
        case 'G':
          ax = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(ARG(0) + 0x4d3), F_OUT);
          goto L7d62;
        case 'I':
          ax = ARG(0) != 0 ? 0x0b8a : 0x0b94;
          goto L7e84;
        case 'L':
        case 'l': {
          uint16_t w = ARG(0);
          L16(-0x1f2) = w;
          ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, (uint16_t)(w & 0x0f), drd8(fmt) == 0x4c ? 1 : 0,
                                   (w & 0x10) != 0 ? 1 : 0, (w & 0x20) != 0 ? 1 : 0,
                                   (uint16_t)((int16_t)(w & 0xc0) >> 6), 0);
          goto L7e84;
        }
        case 'M':
          ax = drd16((uint16_t)((uint16_t)(ARG(0) * 2) + 0x4c2));
          goto L7e84;
        case 'O':
          ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, (uint16_t)((ARG(0) & 0x1e00) >> 9), 0, 0, 0, 0, 0);
          goto L7e84;
        case 'P': {
          int16_t w = (int16_t)ARG(0);
          int16_t q = (int16_t)(w / 100);
          L16(-0x1f4) = (uint16_t)q;
          double lim;
          memcpy(&lim, LIN(dsfp(0x1ff8)), 8);
          /* FILD m16; FCOM m64; FSTP ST(0); FNSTSW AX; SAHF; JB: taken when q < lim or unordered */
          if (!extF80_le(fp80_from_f64(lim), fp80_from_i32(q))) {
            /* IDIV by -100, IMUL 100, ADD: w % 100 with the sign of w */
            int16_t r = (int16_t)(uint16_t)((uint16_t)w + (uint16_t)((uint16_t)(int16_t)(w / -100) * 100));
            pushw((uint16_t)r);
            pushw((uint16_t)(int16_t)(w / 100));
            ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(drd16(0x018a)), MK_FP(rt_ss, rt_sp));
            popw(2);
          } else {
            pushw((uint16_t)(int16_t)(w / 100));
            ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(drd16(0x0178)), MK_FP(rt_ss, rt_sp));
            popw(1);
          }
          F_OUT = (uint16_t)(F_OUT + ax);
          goto L7eb2;
        }
        case 'S': {
          if (ARG(0) == drd16(0x014c)) goto L7eb2;
          L32(-0x1f0) = drd32(0x0ba4);
          L8(-0x1ec) = drd8(0x0ba8);
          L16(-6) = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, ARG(0), 0, 0, 0, 0, 0);
          ds_strcat(bpoff(-0x1f0), L16(-6));
          ds_strcat(bpoff(-0x1f0), 0x0ba9);
          ax = bpoff(-0x1f0);
          goto L7e84;
        }
        case 'U':
        case 'V':
        case 'v': {
          uint32_t v = rd32(aseg, aoff);
          L32(-4) = v;
          aoff = (uint16_t)(aoff + 4);
          pushd(v);
          ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(drd16(0x0170)), MK_FP(rt_ss, rt_sp));
          popw(2);
          F_OUT = (uint16_t)(F_OUT + ax);
          if (drd8(fmt) == 0x76) goto L7eb6;
          if (drd8(fmt) == 0x56) L16(-0x10) = ARG(0);
          L16(-6) = drd16((uint16_t)((uint16_t)(L16(-0x10) * 2) + 0x0b66));
          ds_strcpy(F_OUT, L16(-6));
          F_OUT = (uint16_t)(F_OUT + ds_strlen(L16(-6)));
          goto L7eb6;
        }
        case 'X':
          goto L7eb2;
        case 'Z': {
          uint16_t si = ARG(0);
          if (si == 0) goto L7eb6;
          if ((si & (uint16_t)(si - 1)) == 0) {
            uint16_t di = 0;
            while ((si & 1) == 0) {
              di = (uint16_t)(di + 1);
              si = (uint16_t)(si >> 1);
            }
            ax = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, di, 0, 1, 1, 0, 0);
            goto L7e84;
          }
          L16(-0xc) = si;
          L32(-4) = 0;
          if ((int16_t)drd16(0x0078) <= 0) goto L7eb2;
          do {
            if ((L8(-0xc) & 1) != 0) {
              if ((int16_t)L16(-2) > 0) {
                if ((L16(-0xc) & 0xfffe) != 0) {
                  dwr8(F_OUT, 0x2c);
                  F_OUT = (uint16_t)(F_OUT + 1);
                  dwr8(F_OUT, 0x20);
                  F_OUT = (uint16_t)(F_OUT + 1);
                } else {
                  for (uint16_t k = 0; k < 6; k++) dwr8((uint16_t)(F_OUT + k), drd8((uint16_t)(0x0b9e + k)));
                  F_OUT = (uint16_t)(F_OUT + 5);
                }
              }
              L16(-6) = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, L16(-4), 0, 1, 1, 0, 0);
              ds_strcpy(F_OUT, L16(-6));
              F_OUT = (uint16_t)(F_OUT + ds_strlen(L16(-6)));
              L16(-2) = (uint16_t)(L16(-2) + 1);
            }
            L16(-0xc) = (uint16_t)(L16(-0xc) >> 1);
            L16(-4) = (uint16_t)(L16(-4) + 1);
          } while ((int16_t)L16(-4) < (int16_t)drd16(0x0078));
          goto L7eb2;
        }
        case 'e':
          ax = drd16((uint16_t)((uint16_t)(ARG(0) * 2) + 0x48e));
          goto L7e84;
        case 'f':
        case 'h':
        case 'r':
        case 't':
        case 'y': {
          ds_strcpy(F_OUT, 0x5764);
          F_OUT = (uint16_t)(F_OUT + ds_strlen(0x5764));
          uint8_t k = drd8(fmt);
          uint16_t f;
          if (k == 'y') {
            ds_strcat(F_OUT, 0x0b86);
            F_OUT = (uint16_t)(F_OUT + 3);
            goto L7eb6;
          }
          if (k == 'f') {
            if (drd16(0x014c) == 0xffff) goto Lt;
            f = 0x0b72;
            goto Lfmt;
          }
          if (k == 'h') goto Lh;
          if (k == 'r') {
            f = 0x0b81;
            goto Lfmt;
          }
          /* 't' */
        Lt:
          if (drd16(0x014c) == 0xffff) goto Lh;
          f = 0x0b77;
        Lfmt:
          pushw((uint16_t)(drd16(0x014c) + 1));
          ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(f), MK_FP(rt_ss, rt_sp));
          popw(1);
          L16(-2) = ax;
          goto L7d62;
        Lh:
          ds_strcat(F_OUT, 0x0b7c);
          F_OUT = (uint16_t)(F_OUT + 4);
          goto L7eb6;
        }
        case 'i':
          pushw(ARG(0));
          ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(drd16(0x016a)), MK_FP(rt_ss, rt_sp));
          popw(1);
          L16(-2) = ax;
          goto L7d62;
        case 'j':
          ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, (uint16_t)(ARG(0) + 0x54));
          goto L7e84;
        case 'k': {
          L16(-8) = ARG(0);
          aoff = (uint16_t)(aoff + 2);
          L16(-6) = (uint16_t)((L16(-6) & 0xff00) | rd8(aseg, aoff));
          const uint16_t a[1] = {bpoff(-8)};
          (void)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, a, 1);
          uint16_t n = far_strcpy(F_OUT, L16(-2), (uint16_t)(L16(-4) + 8));
          F_OUT = (uint16_t)(F_OUT + n);
          goto L7eb2;
        }
        case 'm': {
          uint16_t bx = ARG(0);
          L16(-6) = drd16((uint16_t)((uint16_t)(bx + bx) + 0x464));
          L16(-0x10) = bx;
          goto L7e87;
        }
        case 'n': {
          uint16_t w0 = ARG(0);
          if (w0 == 0xfffe) {
            aoff = (uint16_t)(aoff + 2);
            ax = (uint16_t)CALL_ORIG(7, 0x1902, FAR_CDECL, 1, ARG(0));
            goto L7e84;
          }
          if (w0 == 0xffff) {
            aoff = (uint16_t)(aoff + 2);
            goto Lo;
          }
          L16(-6) = (uint16_t)CALL_ORIG(7, 0x25ce, FAR_CDECL, 4, 0, 0xffff, w0, ARG(2));
          aoff = (uint16_t)(aoff + 2);
          goto L7e87;
        }
        case 'g':
          ax = (uint16_t)CALL_ORIG(7, 0x1902, FAR_CDECL, 1, ARG(0));
          goto L7e84;
        case 'o':
        Lo:
          if ((rd8(aseg, (uint16_t)(aoff + 1)) & 0x80) != 0) goto Ls;
          ax = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, ARG(0));
          goto L7e84;
        case 'p':
          ax = (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, ARG(0));
          goto L7e84;
        case 'u':
          pushw(ARG(0));
          ax = (uint16_t)_wsprintf(dsfp(F_OUT), dsfp(0x0bb1), MK_FP(rt_ss, rt_sp));
          popw(1);
          goto L7d62;
        case 'w':
          ds_strcpy(F_OUT, 0x5866);
          F_OUT = (uint16_t)(F_OUT + ds_strlen(0x5866));
          goto L7eb6;
        case 'z': {
          uint16_t w = ARG(0);
          uint16_t pl = (uint16_t)((int16_t)w >> 5);
          L16(-2) = pl;
          uint16_t bx = (uint16_t)(w & 0x1f);
          uint16_t si = (uint16_t)(pl << 2);
          if (bx >= 0x10) {
            L16(-0xa) = (uint16_t)(drd16((uint16_t)(si + 0x10c)) + (uint16_t)(bx * 0x8d) - 0x8d0);
            L16(-8) = drd16((uint16_t)(si + 0x10e));
          } else {
            L16(-0xa) = (uint16_t)(drd16((uint16_t)(si + 0xbe)) + (uint16_t)(bx * 0x8d));
            L16(-8) = drd16((uint16_t)(si + 0xc0));
          }
          if (L16(-2) != drd16(0x014c)) {
            L16(-6) = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, L16(-2), 0, 0, 1, 0, 0);
            ds_strcpy(F_OUT, L16(-6));
            F_OUT = (uint16_t)(F_OUT + ds_strlen(L16(-6)));
            dwr8(F_OUT, 0x20);
            F_OUT = (uint16_t)(F_OUT + 1);
          }
          {
            uint16_t n = far_strcpy(F_OUT, L16(-8), (uint16_t)(L16(-0xa) + 8));
            (void)n;
            F_OUT = (uint16_t)(F_OUT + ds_strlen(F_OUT));
          }
          goto L7eb2;
        }
        case 's':
        Ls:
          ax = (uint16_t)CALL_ORIG(7, 0x1a70, FAR_CDECL, 1, (uint16_t)(ARG(0) | 0x8000));
          goto L7e84;
        default:
          goto L7797;
      }
    }
  L7797:
    dwr8(F_OUT, drd8(fmt));
    F_OUT = (uint16_t)(F_OUT + 1);
    goto L7eb6;
  L7d62:
    F_OUT = (uint16_t)(F_OUT + ax);
    goto L7eb2;
  L7e84:
    L16(-6) = ax;
  L7e87:
    ds_strcpy(F_OUT, L16(-6));
    F_OUT = (uint16_t)(F_OUT + ds_strlen(L16(-6)));
  L7eb2:
    aoff = (uint16_t)(aoff + 2);
  L7eb6:
    fmt = (uint16_t)(fmt + 1);
    if (drd8(fmt) == 0) break;
  }
L7ec3:
  dwr8(F_OUT, 0);
  FRAME_LEAVE();
  return 0x553e;
#undef ARG
}
#undef F_OUT

/* format_message_id: 6:0x80c8 (Ghidra FUN_1028_80c8, alias fn_06_80c8).
 * 24 bytes. PUSH BP; MOV BP,SP. FAR CDECL (id, args far pointer): the text of
 * message id, 6:0x7778(6:0x8864(id), args). AX = 0x553e. */
DECOMP(6, 0x80c8, format_message_id, FAR_CDECL)
uint16_t format_message_id(uint16_t id, uint32_t args)
{
  FRAME_ENTER(0);
  pushd(args);
  uint16_t f = call6_1(0x8864, id);
  popw(2);
  const uint16_t a[3] = {f, (uint16_t)args, (uint16_t)(args >> 16)};
  uint16_t r = (uint16_t)call6(0x7778, a, 3);
  FRAME_LEAVE();
  return r;
}

/* format_string_id: 6:0x80e0 (Ghidra FUN_1028_80e0, alias fn_06_80e0).
 * 25 bytes. PUSH BP; MOV BP,SP. FAR CDECL (string id, args far pointer):
 * 6:0x7778(8:0x8df4(id), args), a format from the string table. AX = 0x553e. */
DECOMP(6, 0x80e0, format_string_id, FAR_CDECL)
uint16_t format_string_id(uint16_t id, uint32_t args)
{
  FRAME_ENTER(0);
  pushd(args);
  uint16_t f = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id);
  popw(2);
  const uint16_t a[3] = {f, (uint16_t)args, (uint16_t)(args >> 16)};
  uint16_t r = (uint16_t)call6(0x7778, a, 3);
  FRAME_LEAVE();
  return r;
}

/* message_text: 6:0x774a (Ghidra FUN_1028_774a, alias fn_06_774a).
 * 46 bytes. ENTER 0x12,0. FAR CDECL (idx). The text of message idx: reads it
 * with 6:0x7668 into BP-0x12 (id, subject, values) and returns 6:0x80c8(id, the values at BP-0xe); when the
 * message does not exist DS:0x553e becomes "" and 0x553e is returned. AX only. */
DECOMP(6, 0x774a, message_text, FAR_CDECL)
uint16_t message_text(uint16_t idx)
{
  FRAME_ENTER(0x12);
  const uint16_t a[2] = {idx, bpoff(-0x12)};
  uint16_t r;
  if ((uint16_t)call6(0x7668, a, 2) == 0) {
    dwr8(0x553e, 0);
    r = 0x553e;
  } else {
    const uint16_t b[3] = {L16(-0x12), bpoff(-0xe), rt_ss};
    r = (uint16_t)call6(0x80c8, b, 3);
  }
  FRAME_LEAVE();
  return r;
}

/* (a - b - k) / 2 + b as CWD; SUB AX,DX; SAR AX,1 computes it: a signed division by 2 that rounds toward 0. */
static inline uint16_t mid16(uint16_t a, uint16_t b, uint16_t k)
{
  int16_t v = (int16_t)(uint16_t)(a - b - k);
  return (uint16_t)((uint16_t)(int16_t)(v / 2) + b);
}

/* draw_message_icons: 6:0x6fc6 (Ghidra FUN_1028_6fc6, alias fn_06_6fc6).
 * 717 bytes. ENTER 0x16,0; PUSH DI; PUSH SI. FAR CDECL (hdc, rect, a near
 * pointer to a RECT: +2 top, +4 right, +6 bottom). Draws the small pictures of the message window from the
 * bitmap DS:0x5a54 (and its mask DS:0x2884) through a memory DC: with a message shown (0 <= DS:0xab6 <
 * DS:0xab4) its "filtered" (14 x 12 from y 0x2a, mask y 0xe) or plain (15 x 14 from y 0x1c, mask y 0)
 * picture at the left, colours set to black on white (SetTextColor / SetBkColor, restored at the end); when
 * some id is both occurred and filtered, the filter button at the right (two PatBlt lines with the brushes
 * DS:0x16 / DS:0x14 and a 15 x 15 picture, y 0x36 / mask 0x1a when DS:0xaba is set, else 0x45 / 0x29),
 * otherwise DS:0xaba becomes 0. Bit 0 of DS:0x791 (the comment mode) skips both. Unless bit 2 of DS:0x80 is
 * set, a black 0x11 x 0xb box and a 15 x 9 picture (y 0x38) at the right end. The BitBlt destination is
 * (BP-2, BP-4) as pushed (PUSH DWORD [BP-4]): the "x" is the word at BP-2. In the comment mode and when no
 * message is shown the colours restored are the uninitialised words at BP-0xe / BP-0x12 (the mirrored frame
 * keeps them the same as the original's). Returns nothing. */
DECOMP(6, 0x6fc6, draw_message_icons, FAR_CDECL)
void draw_message_icons(uint16_t hdc, uint16_t rect)
{
  FRAME_ENTER_REGS(0x16, 2);
  uint16_t di, si;
  L16(-0x16) = CreateCompatibleDC(hdc);
  L16(-0x14) = SelectObject(L16(-0x16), drd16(0x5a54));
  if ((drd8(0x0791) & 1) != 0) goto L7052;
  si = mid16(drd16((uint16_t)(rect + 6)), drd16((uint16_t)(rect + 2)), 0x0b);
  if ((int16_t)drd16(0x0ab6) >= 0 && (int16_t)drd16(0x0ab4) > (int16_t)drd16(0x0ab6)) {
    uint16_t v = call6_1(0x7646, drd16(0x0ab6));
    if (filter_bit(v) != 0) {
      di = 0x0e;
      L16(-8) = 0x0e;
      L16(-6) = 0x0c;
      L16(-2) = (uint16_t)(si - 1);
      L16(-4) = si;
      L16(-0xa) = 0x2a;
    } else {
      L16(-8) = 0;
      di = 0x0f;
      L16(-6) = 0x0e;
      L16(-2) = si;
      L16(-4) = (uint16_t)(si - 3);
      L16(-0xa) = 0x1c;
    }
    L32(-0xe) = SetTextColor(hdc, 0);
    L32(-0x12) = SetBkColor(hdc, 0x00ffffff);
    (void)BitBlt(hdc, (int16_t)L16(-2), (int16_t)L16(-4), (int16_t)di, (int16_t)L16(-6), L16(-0x16), 0,
                 (int16_t)L16(-0xa), 0x008800c6);
    L16(-0x14) = SelectObject(L16(-0x16), drd16(0x2884));
    (void)BitBlt(hdc, (int16_t)L16(-2), (int16_t)L16(-4), (int16_t)di, (int16_t)L16(-6), L16(-0x16), 0,
                 (int16_t)L16(-8), 0x00ee0086);
  }
  {
    uint16_t k = 0;
    di = rect;
    while (k < 0x31 && (drd8((uint16_t)(k + 0x534e)) & drd8((uint16_t)(k + 0x5508))) == 0) k++;
    L16(-2) = k;
    if (k == 0x31) {
      dwr16(0x0aba, 0);
      goto L7055;
    }
  }
  if (drd16(0x0aba) != 0) {
    L16(-8) = 0x1a;
    L16(-0xa) = 0x36;
  } else {
    L16(-8) = 0x29;
    L16(-0xa) = 0x45;
  }
  si = hdc;
  {
    uint16_t top = drd16((uint16_t)(di + 2)), right = drd16((uint16_t)(di + 4)), bottom = drd16((uint16_t)(di + 6));
    L16(-2) = (uint16_t)(top - bottom + right - 1);
    L16(-6) = SelectObject(si, drd16(0x0016));
    (void)PatBlt(si, (int16_t)L16(-2), (int16_t)drd16((uint16_t)(di + 2)), 1,
                 (int16_t)(uint16_t)(drd16((uint16_t)(di + 6)) - drd16((uint16_t)(di + 2))), 0x00f00021);
    (void)SelectObject(si, drd16(0x0014));
    (void)PatBlt(si, (int16_t)(uint16_t)(L16(-2) + 1), (int16_t)drd16((uint16_t)(di + 2)), 1,
                 (int16_t)(uint16_t)(drd16((uint16_t)(di + 6)) - drd16((uint16_t)(di + 2))), 0x00f00021);
    (void)SelectObject(si, L16(-6));
    L16(-4) = mid16(drd16((uint16_t)(di + 6)), drd16((uint16_t)(di + 2)), 0x0f);
    L16(-2) = (uint16_t)(L16(-4) - drd16((uint16_t)(di + 6)) + drd16((uint16_t)(di + 4)));
    (void)SelectObject(L16(-0x16), drd16(0x5a54));
    (void)BitBlt(si, (int16_t)L16(-2), (int16_t)L16(-4), 0x0f, 0x0f, L16(-0x16), 0, (int16_t)L16(-0xa), 0x008800c6);
    (void)SelectObject(L16(-0x16), drd16(0x2884));
    (void)BitBlt(si, (int16_t)L16(-2), (int16_t)L16(-4), 0x0f, 0x0f, L16(-0x16), 0, (int16_t)L16(-8), 0x00ee0086);
  }
  goto L7207;
L7052:
L7055:
  di = rect;
  si = hdc;
L7207:
  (void)SetTextColor(si, L32(-0xe));
  (void)SetBkColor(si, L32(-0x12));
  if ((drd8(0x0080) & 4) == 0) {
    (void)SelectObject(L16(-0x16), drd16(0x2884));
    L16(-2) = (uint16_t)(drd16((uint16_t)(di + 4)) - 0x2d);
    uint16_t cx = L16(-2);
    L16(-4) = mid16(drd16((uint16_t)(di + 6)), drd16((uint16_t)(di + 2)), 7);
    (void)PatBlt(si, (int16_t)(uint16_t)(cx - 1), (int16_t)(uint16_t)(L16(-4) - 1), 0x11, 0x0b, 0x00000042);
    (void)BitBlt(si, (int16_t)L16(-2), (int16_t)L16(-4), 0x0f, 9, L16(-0x16), 0, 0x38, 0x00cc0020);
  }
  (void)SelectObject(L16(-0x16), L16(-0x14));
  (void)DeleteDC(L16(-0x16));
  FRAME_LEAVE();
}

/* update_message_controls: 6:0x6a68 (Ghidra FUN_1028_6a68, alias fn_06_6a68).
 * 1164 bytes. ENTER 0x70,0; PUSH DI; PUSH SI. FAR CDECL (hwnd). Brings the
 * controls of the message window in step with the store; nothing when hwnd is 0 or DS:0x15fe is set. The
 * comment controls (edit DS:0x59e6, combo box DS:0x53e0, button DS:0x4da4) are shown in the comment mode
 * (bit 0 of DS:0x791), hidden otherwise; the "goto" button DS:0x54fe is hidden in it. The button DS:0x4da0
 * reads "Done" in the comment mode, "Reply" past the messages, "View" with bit 4 of DS:0x791, else the
 * text DS:0xae2. Comment mode: the title DS:0x5866 is the string 0x508 of 8:0x8df4 with DS:0xab8 + 1 and
 * DS:0xac6; the controls are laid out in the rectangle DS:0x2532 shrunk by 4 (8:0x96e6) (SetWindowPos; the
 * edit control below the font height DS:0x2686 + 3); "previous" (DS:0x4d9e) is enabled when DS:0xab8 > 0,
 * DS:0x4da0 and DS:0x4da2 always; the comment at DS:0xab8 (list DS:0xac0) is shown: the combo box gets its
 * subject (CB_SETCURSEL), the edit control its text (unpacked by 8:0xa666 into DS:0x53ea when its length
 * +0xa is not negative); without one the selection DS:0xac8 and an empty text. Message mode: the title is
 * the string 0x228 (with the turn DS:0x82 + 2400, the mark '*' or ' ' of DS:0x7e, the message number
 * DS:0xab6 + 1 and the count DS:0xab4 + DS:0xac4) or 0x229 (no messages) of 8:0x8dc0, formatted into
 * DS:0x5866; "previous" / "next" are enabled when 6:0x6f66(0) / 6:0x6ef4(0) find one; and past the messages
 * DS:0x4da0 is enabled, else the "goto" kind of the shown message is worked out from its subject word
 * (6:0x7668 into BP-0x20) into DS:0x415c (0 none, 1 a planet, 2 a fleet that 7:0x14a8 finds, 3 .. 0xb the
 * special subjects 0xfffe .. 0xfff9 and the 0xc000 / 0x4000 kinds, with DS:0x4160 / 0x4156 / 0x4158 the
 * object) and it is 0 for a filtered message (unless DS:0xaba) or none shown; DS:0x4da0 is enabled when it is
 * not 0. Finally the title is copied to DS:0x4a66 and the rectangle DS:0x4b64 of the window DS:0x158 is
 * invalidated. Returns nothing. */
DECOMP(6, 0x6a68, update_message_controls, FAR_CDECL)
void update_message_controls(uint16_t hwnd)
{
  FRAME_ENTER_REGS(0x70, 2);
  uint16_t si, cx;
  if (hwnd == 0 || drd16(0x15fe) != 0) goto out;
  L8(-1) = drd16(0x007e) == 0 ? 0x20 : 0x2a;
  si = (drd8(0x0791) & 1) != 0 ? 5 : 0;
  (void)ShowWindow(drd16(0x59e6), (int16_t)si);
  (void)ShowWindow(drd16(0x53e0), (int16_t)si);
  (void)ShowWindow(drd16(0x4da4), (int16_t)si);
  if ((drd8(0x0791) & 1) != 0) (void)ShowWindow(drd16(0x54fe), 0);
  si = (uint16_t)(drd16(0x0ab4) + drd16(0x0ac4));
  if ((drd8(0x0791) & 1) != 0)
    cx = 0x0b43;
  else if ((int16_t)drd16(0x0ab4) <= (int16_t)drd16(0x0ab6))
    cx = 0x0b4d;
  else if ((drd8(0x0791) & 0x10) != 0)
    cx = 0x0b48;
  else
    cx = drd16(0x0ae2);
  SetWindowText(drd16(0x4da0), dsfp(cx));
  if ((drd8(0x0791) & 1) != 0) {
    pushw(drd16(0x0ac6));
    pushw((uint16_t)(drd16(0x0ab8) + 1));
    uint16_t f = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x0508);
    (void)_wsprintf(dsfp(0x5866), dsfp(f), MK_FP(rt_ss, rt_sp));
    popw(2);
    L32(-0xe) = drd32(0x2532);
    L32(-0xa) = drd32(0x2536);
    (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, bpoff(-0xe), 0xfffc, 0xfffc);
    (void)SetWindowPos(drd16(0x53e0), 0, (int16_t)(uint16_t)(L16(-0xe) + 0x1e), (int16_t)L16(-0xc),
                       (int16_t)(uint16_t)(L16(-0xa) - L16(-0xe) - 0x54), (int16_t)(uint16_t)(L16(-8) - L16(-0xc)), 0x44);
    (void)SetWindowPos(drd16(0x4da4), 0, (int16_t)(uint16_t)(L16(-0xa) - 0x32), (int16_t)L16(-0xc), 0, 0, 0x45);
    L16(-0xc) = (uint16_t)(L16(-0xc) + drd16(0x2686) + 3);
    (void)SetWindowPos(drd16(0x59e6), 0, (int16_t)L16(-0xe), (int16_t)L16(-0xc), (int16_t)(uint16_t)(L16(-0xa) - L16(-0xe)),
                       (int16_t)(uint16_t)(L16(-8) - L16(-0xc)), 0x44);
    (void)EnableWindow(drd16(0x4d9e), (int16_t)drd16(0x0ab8) > 0 ? 1 : 0);
    (void)EnableWindow(drd16(0x4da0), 1);
    (void)EnableWindow(drd16(0x4da2), 1);
    uint16_t nseg, noff;
    noff = drd16(0x0ac0);
    nseg = drd16(0x0ac2);
    L16(-2) = nseg;
    L16(-6) = drd16(0x0ab8);
    for (;;) {
      uint16_t ax = L16(-6);
      L16(-6) = (uint16_t)(L16(-6) - 1);
      if (!((int16_t)ax > 0)) break;
      uint32_t nx = rd32(L16(-2), noff);
      noff = (uint16_t)nx;
      L16(-2) = (uint16_t)(nx >> 16);
    }
    nseg = L16(-2);
    if ((nseg | noff) != 0) {
      (void)API_SP(10, SendMessage(drd16(0x53e0), 0x040e, rd16(nseg, (uint16_t)(noff + 6)), 0));
      nseg = L16(-2);
      if ((int16_t)rd16(nseg, (uint16_t)(noff + 0x0a)) < 0) {
        SetWindowText(drd16(0x59e6), MK_FP(nseg, (uint16_t)(noff + 0x0c)));
      } else {
        L16(-6) = 0x03e8;
        (void)CALL_ORIG(8, 0xa666, FAR_CDECL, 6, (uint16_t)(noff + 0x0c), nseg, rd16(nseg, (uint16_t)(noff + 0x0a)),
                        drd16(0x53ea), drd16(0x53ec), bpoff(-6));
        SetWindowText(drd16(0x59e6), drd32(0x53ea));
      }
    } else {
      (void)API_SP(10, SendMessage(drd16(0x53e0), 0x040e, drd16(0x0ac8), 0));
      SetWindowText(drd16(0x59e6), dsfp(0x0b53));
    }
    goto L6ec2;
  }
  if (si != 0) {
    (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x0228, bpoff(-0x70));
    pushw(si);
    pushw((uint16_t)(drd16(0x0ab6) + 1));
    pushw((uint16_t)(int16_t)(int8_t)L8(-1));
    pushw((uint16_t)(drd16(0x0082) + 0x0960));
    (void)_wsprintf(dsfp(0x5866), bpfp(-0x70), MK_FP(rt_ss, rt_sp));
    popw(4);
  } else {
    (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x0229, bpoff(-0x70));
    pushw((uint16_t)(int16_t)(int8_t)L8(-1));
    pushw((uint16_t)(drd16(0x0082) + 0x0960));
    (void)_wsprintf(dsfp(0x5866), bpfp(-0x70), MK_FP(rt_ss, rt_sp));
    popw(2);
  }
  {
    uint16_t h = drd16(0x4d9e);
    pushw(h);
    uint16_t r = call6_1(0x6f66, 0);
    popw(1);
    (void)EnableWindow(h, r != 0xffff ? 1 : 0);
  }
  {
    uint16_t h = drd16(0x4da2);
    pushw(h);
    uint16_t r = call6_1(0x6ef4, 0);
    popw(1);
    (void)EnableWindow(h, r != 0xffff ? 1 : 0);
  }
  if ((int16_t)drd16(0x0ab4) <= (int16_t)drd16(0x0ab6)) {
    (void)EnableWindow(drd16(0x4da0), 1);
    goto L6ec2;
  }
  if (drd16(0x0ab4) == 0 || (int16_t)drd16(0x0ab4) <= (int16_t)drd16(0x0ab6)) goto L6e64;
  {
    const uint16_t a[2] = {drd16(0x0ab6), bpoff(-0x20)};
    if ((uint16_t)call6(0x7668, a, 2) == 0) goto L6e64;
  }
  {
    uint16_t w = L16(-0x1e);
    if (w == 0xffff) {
      dwr16(0x415c, 0);
      goto L6e64;
    }
    dwr16(0x4160, (uint16_t)(w & 0x7fff));
    if (w == 0xfffe) {
      dwr16(0x415c, 3);
    } else if (w == 0xfffd) {
      dwr16(0x415c, 5);
    } else if (w == 0xfffc) {
      dwr16(0x415c, 8);
    } else if (w == 0xfffb) {
      dwr16(0x415c, 9);
    } else if (w == 0xfffa) {
      dwr16(0x415c, 0x0a);
      dwr16(0x4156, L16(-0x1c));
    } else if (w == 0xfff9) {
      dwr16(0x415c, 0x0b);
    } else if ((L8(-0x1d) & 0xc0) == 0xc0) {
      dwr16(0x415c, 4);
    } else if ((L8(-0x1d) & 0x40) != 0) {
      uint16_t o = (uint16_t)(L16(-0x1e) & 0x3fff);
      dwr16(0x4160, o);
      if (o == 0x0800) {
        dwr16(0x415c, 7);
      } else {
        dwr16(0x415c, 6);
        dwr16(0x4156, L16(-0x1c));
        dwr16(0x4158, L16(-0x1a));
      }
    } else if ((int16_t)L16(-0x1e) >= 0) {
      dwr16(0x415c, 1);
    } else {
      uint32_t r = CALL_ORIG(7, 0x14a8, FAR_CDECL, 1, drd16(0x4160));
      dwr16(0x415c, r != 0 ? 2 : 0);
    }
  }
L6e64:
  if (drd16(0x415c) != 0) {
    if ((int16_t)drd16(0x0ab6) < 0) {
      dwr16(0x415c, 0);
    } else if (drd16(0x0aba) == 0) {
      uint16_t v = call6_1(0x7646, drd16(0x0ab6));
      uint16_t b = drd8((uint16_t)((uint16_t)(int16_t)((int16_t)v >> 3) + 0x5508));
      uint16_t v2 = call6_1(0x7646, drd16(0x0ab6));
      if (((uint16_t)(1u << (v2 & 7)) & b) != 0) dwr16(0x415c, 0);
    }
  }
  (void)EnableWindow(drd16(0x4da0), drd16(0x415c) != 0 ? 1 : 0);
L6ec2:
  (void)ds_strcpy(0x4a66, 0x5866);
  InvalidateRect(drd16(0x0158), dsfp(0x4b64), 1);
out:
  FRAME_LEAVE();
}

/* 6:0x7ed0 MSGDLG (exported; Ghidra MSGDLG), 504 bytes. ENTER 0x130,0; PUSH DI; PUSH SI. FAR PASCAL (hwnd, msg, wParam,
 * lParam); RETF 0xa. The dialog that asks for a number (the "goto" dialog of the message window):
 *   WM_PAINT: the string 0x4b9 + (signed byte DS:0x588e) of 8:0x8dc0 drawn (DrawText 0x810) in the font
 *     DS:0x2930, black on DS:0x253a, in the rectangle from x 8 to the client width - 8, y from 8 below the
 *     bottom of the control 0x10c (GetWindowRect + ScreenToClient of its bottom-right corner) 0x64 high
 *   WM_ERASEBKGND: FillRect of the client area with the brush DS:0x10 (wParam is the DC)
 *   WM_CTLCOLOR: for HIWORD(lParam) 6 (static) SetBkColor(wParam, DS:0x253a) and the brush DS:0x10
 *   WM_INITDIALOG: control 0x10c limited to 8 characters (0x415), its text DS:0x5866 = "", 8:0x97a2(hwnd,
 *     &(-1, -1), 1) (the dialog's position)
 *   WM_COMMAND: IDOK reads the text of 0x10c (9 bytes into DS:0x5866) and asks 7:0x3ef4(DS:0x5866, 0); when
 *     it refuses, 8:0x8d94(0x4bc, 0x10) (a message box) and the focus back to 0x10c (result 0); else, and
 *     for IDCANCEL, EndDialog(hwnd, wParam == IDOK). 0x76 opens the help (WinHelp(hwnd, DS:0x10a, 1, 0xdbc)).
 * Returns 1 for a handled message, else 0 (AX). */
DECOMP(6, 0x7ed0, MSGDLG, FAR_PASCAL)
uint16_t MSGDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x130, 2);
  uint16_t r = 1;
  uint16_t si, di;
  switch (msg) {
    case 0x000f:
      di = hwnd;
      si = BeginPaint(di, bpfp(-0x30));
      GetClientRect(di, bpfp(-0x10));
      GetWindowRect(GetDlgItem(di, 0x010c), bpfp(-8));
      ScreenToClient(di, bpfp(-4));
      L16(-8) = 8;
      L16(-4) = (uint16_t)(L16(-0xc) - 8);
      L16(-6) = (uint16_t)(L16(-2) + 8);
      L16(-2) = (uint16_t)(L16(-6) + 0x64);
      (void)SelectObject(si, drd16(0x2930));
      (void)SetBkColor(si, drd32(0x253a));
      (void)SetTextColor(si, 0);
      {
        pushw(si);
        pushw(rt_ss);
        pushw(bpoff(-0x130));
        uint16_t n = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)((uint16_t)(int16_t)(int8_t)drd8(0x588e) + 0x04b9),
                                         bpoff(-0x130));
        popw(3);
        (void)DrawText(si, bpfp(-0x130), (int16_t)n, bpfp(-8), 0x0810);
      }
      EndPaint(di, bpfp(-0x30));
      break;
    case 0x0014:
      GetClientRect(hwnd, bpfp(-0x10));
      (void)FillRect(wParam, bpfp(-0x10), drd16(0x0010));
      break;
    case 0x0019:
      if ((uint16_t)(lParam >> 16) != 6) {
        r = 0;
        break;
      }
      (void)SetBkColor(wParam, drd32(0x253a));
      r = drd16(0x0010);
      break;
    case 0x0110:
      si = hwnd;
      L16(-4) = 0xffff;
      L16(-2) = 0xffff;
      dwr8(0x5866, 0);
      (void)API_SP(12, SendDlgItemMessage(si, 0x010c, 0x0415, 8, 0));
      SetWindowText(GetDlgItem(si, 0x010c), dsfp(0x5866));
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, si, bpoff(-4), 1);
      break;
    case 0x0111:
      si = wParam;
      if (si != 1 && si != 2) {
        if (si != 0x76) {
          r = 0;
          break;
        }
        (void)WinHelp(hwnd, dsfp(drd16(0x010a)), 1, 0x00000dbc);
        break;
      }
      if (si == 1) {
        (void)GetDlgItemText(hwnd, 0x010c, dsfp(0x5866), 9);
        if ((uint16_t)CALL_ORIG(7, 0x3ef4, FAR_CDECL, 2, 0x5866, 0) == 0) {
          (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, 0x04bc, 0x0010);
          (void)SetFocus(GetDlgItem(hwnd, 0x010c));
          r = 0;
          break;
        }
      }
      (void)EndDialog(hwnd, si == 1 ? 1 : 0);
      break;
    default:
      r = 0;
      break;
  }
  FRAME_LEAVE();
  return r;
}

/* The two "is the shown message's filter bit set" tests of MESSAGEWNDPROC: 6:0x7646 called twice, the byte
 * from the first id and the bit from the second (they are the same message). */
static uint16_t shown_filtered(void)
{
  uint16_t v = call6_1(0x7646, drd16(0x0ab6));
  uint16_t b = drd8((uint16_t)((uint16_t)(int16_t)((int16_t)v >> 3) + 0x5508));
  uint16_t v2 = call6_1(0x7646, drd16(0x0ab6));
  return (uint16_t)((uint16_t)(1u << (v2 & 7)) & b);
}

/* 6:0x0000 MESSAGEWNDPROC (exported; Ghidra MESSAGEWNDPROC). The inventory's size (2677) stops at the 11-word jump table at
 * cs:0xa76; the procedure runs to 0x0e73 (3700 bytes). ENTER 0x4a,0; PUSH DI; PUSH SI. FAR PASCAL (hwnd,
 * msg, wParam, lParam); RETF 0xa; returns DX:AX. The window procedure of the message window:
 *   WM_CREATE: DS:0x158 = hwnd; four BUTTON children (DS:0x4d9e .. 0x4da4: previous, goto, next, delete; texts
 *     DS:0xae0 + 2k), the combo box "MsgDD" DS:0x53e0 (Everybody and the player names of 7:0x0b84), the
 *     edit controls DS:0x59e6 (the comment, limit 0x3c8) and DS:0x54fe (long messages), all with the font
 *     DS:0x2930; 6:0x6a68(hwnd); result 0.
 *   WM_SIZE: the buttons along the right edge, the rectangles DS:0x2532 (the text) and DS:0x4b64 (the title
 *     line), the comment controls inside DS:0x2532 shrunk by 4 (8:0x96e6); then DefWindowProc.
 *   WM_PAINT: the title DS:0x4a66 (as many characters as fit, GetTextExtent) with 8:0x9958 / 8:0x9318, the
 *     pictures (6:0x6fc6), a 3D frame around DS:0x2532 (PatBlt with DS:0x16 / DS:0x14), and the text: in the
 *     comment mode (bit 0 of DS:0x791) only "To:" (8:0x9398); past the messages the comment DS:0xab6 -
 *     DS:0xab4 of the chain DS:0xabc, as "From:  <name>\r\nTo:  <name or Everybody>\r\n<text>" in DS:0x53ea
 *     (8:0xa666 unpacks a packed text); otherwise the message text (6:0x774a, or the strings 0x21 / 0x22 of
 *     8:0x8df4 for a filtered message / none shown), "FILTERED" (8:0x9482) on a filtered one, drawn with
 *     DrawText 0x810 when it fits (DT_CALCRECT 0xc10 first) or else in the edit control DS:0x54fe; result 0.
 *   WM_ERASEBKGND: FillRect with DS:0x10, result 1. WM_CTLCOLOR for DS:0x54fe: SetBkColor DS:0x253a, the
 *     brush DS:0x10. WM_SETCURSOR: the cursor DS:0x5442 where 6:0x7294 finds something (result 1).
 *   WM_GETMINMAXINFO: the minimum tracking size from DS:0x266c and DS:0x267a, then DefWindowProc.
 *   WM_KEYDOWN: Up / Down (previous / next), Home / End (outside the comment mode). WM_CHAR: Return (goto),
 *     '+' (toggle the filter of the shown message), '-' (toggle "starred only" DS:0xaba when some filtered id
 *     occurred, and move to a message that fits).
 *   WM_COMMAND from the buttons (notification 0; the focus goes back to DS:0x2806 then): previous / next
 *     (with Shift: the first / the last; in the comment mode 6:0x8640(-1 / 1)), delete (6:0x8640(0x3e8)),
 *     goto: in the comment mode or past the messages it toggles the comment mode (L0d04: 6:0x8640(0) when
 *     leaving it, else the subject DS:0xac8 / the comment index DS:0xab8 of the shown comment), else the
 *     action of DS:0x415c (1 planet 9:0x2ed8, 2 fleet 10:0x26b2, 3 / 5 / 7 / 8 / 11 commands posted to
 *     DS:0x2806, 4 a tech item (2:0x50f8 on DS:0x252a), 6 7:0x063e / 29:0x0000, 9 the dialog MSGDLG
 *     (template 0x56) and 7:0x3ef4, 10 7:0x01a0 / 11:0x62f2 / 11:0x5142); then, with bit 3 of DS:0x791, bit 2
 *     of DS:0x53f0 and 31:0x61b0.
 *   WM_LBUTTONDOWN / WM_LBUTTONDBLCLK: 6:0x7294: 1 the filter toggle, 2 "starred only", 3 the comment mode.
 *   Everything else, and the cases noted: DefWindowProc. */
DECOMP(6, 0x0000, MESSAGEWNDPROC, FAR_PASCAL)
uint32_t MESSAGEWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x4a, 2);
  uint32_t ret = 0;
  uint16_t si, di, ax;
  switch (msg) {
    case 0x0001: goto L0068;
    case 0x0005: goto L0204;
    case 0x000f: goto L031e;
    case 0x0014: goto L079c;
    case 0x0019: goto L07c6;
    case 0x0020: goto L07e8;
    case 0x0024: goto L081c;
    case 0x0100: goto L0846;
    case 0x0102: goto L0882;
    case 0x0111: goto L092e;
    case 0x0201:
    case 0x0203: goto L0cd6;
    default: goto Ldef;
  }

L0068: /* WM_CREATE */
  si = hwnd;
  dwr16(0x0158, si);
  for (L16(-0x14) = 0; (int16_t)L16(-0x14) < 4; L16(-0x14) = (uint16_t)(L16(-0x14) + 1)) {
    uint16_t bx = (uint16_t)(L16(-0x14) * 2);
    L16(-0x46) = bx;
    int16_t cx = L16(-0x14) == 3 ? 0x32 : 0x2c;
    uint16_t v = drd16(0x267a);
    int16_t cy = (int16_t)(uint16_t)((uint16_t)((int16_t)(uint16_t)(v + v + v) >> 1) - 1);
    uint16_t h = API_SP(30, CreateWindow(dsfp(0x0ae8), dsfp(drd16((uint16_t)(bx + 0x0ae0))), 0x40000000, 0x64, 0x64, cx, cy,
                                         si, 0, drd16(0x54f4), 0));
    dwr16((uint16_t)(L16(-0x46) + 0x4d9e), h);
    (void)API_SP(10, SendMessage(drd16((uint16_t)(L16(-0x14) * 2 + 0x4d9e)), 0x30, drd16(0x2930), 0));
  }
  dwr16(0x53e0, API_SP(30, CreateWindow(dsfp(0x0af5), dsfp(0x0aef), 0x40200003, 0x64, 0x64, 0xc8, 0x50, si, 0, drd16(0x54f4), 0)));
  (void)API_SP(10, SendMessage(drd16(0x53e0), 0x30, drd16(0x2930), 0));
  dwr16(0x59e6, API_SP(30, CreateWindow(dsfp(0x0afe), 0, 0x40800044, 0x64, 0x64, 0xc8, 0x32, si, 0, drd16(0x54f4), 0)));
  (void)API_SP(10, SendMessage(drd16(0x59e6), 0x0415, 0x03c8, 0));
  (void)API_SP(10, SendMessage(drd16(0x59e6), 0x30, drd16(0x2930), 0));
  dwr16(0x54fe, API_SP(30, CreateWindow(dsfp(0x0b03), 0, 0x40a00844, 0x64, 0x64, 0xc8, 0x32, si, 0, drd16(0x54f4), 0)));
  (void)call6_1(0x6a68, si);
  (void)API_SP(10, SendMessage(drd16(0x53e0), 0x0403, 0, dsfp(0x0b08)));
  L16(-0x14) = 0;
  if ((int16_t)drd16(0x0078) > 0) {
    do {
      uint16_t nm = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, L16(-0x14), 1, 1, 1, 0, 0);
      (void)API_SP(10, SendMessage(drd16(0x53e0), 0x0403, 0, dsfp(nm)));
      L16(-0x14) = (uint16_t)(L16(-0x14) + 1);
    } while ((int16_t)L16(-0x14) < (int16_t)drd16(0x0078));
  }
  (void)API_SP(10, SendMessage(drd16(0x53e0), 0x040e, 0, 0));
  goto L0e59;

L0204: /* WM_SIZE */
  si = (uint16_t)lParam;
  di = (uint16_t)(lParam >> 16);
  L16(-2) = si;
  L16(-4) = di;
  for (L16(-0x14) = 0; (int16_t)L16(-0x14) < 3; L16(-0x14) = (uint16_t)(L16(-0x14) + 1)) {
    uint16_t v = drd16(0x267a);
    uint16_t cx = (uint16_t)(v + v);
    uint16_t a = (uint16_t)((uint16_t)((int16_t)(uint16_t)(v + v + v) >> 1) + 2);
    cx = (uint16_t)(cx + (uint16_t)(a * L16(-0x14)) + 3);
    (void)API_SP(14, SetWindowPos(drd16((uint16_t)(L16(-0x14) * 2 + 0x4d9e)), 0, (int16_t)(uint16_t)(si - 0x30), (int16_t)cx, 0, 0, 0x55));
  }
  SetRect(dsfp(0x2532), 4, (int16_t)(uint16_t)(drd16(0x267a) * 2 + 3), (int16_t)(uint16_t)(si - 0x34), (int16_t)(uint16_t)(di - 4));
  SetRect(dsfp(0x4b64), 4, 4, (int16_t)(uint16_t)(si - 4), (int16_t)(uint16_t)((uint16_t)(drd16(0x267a) - 2) * 2));
  L32(-0xc) = drd32(0x2532);
  L32(-8) = drd32(0x2536);
  (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, bpoff(-0xc), 0xfffc, 0xfffc);
  (void)API_SP(14, SetWindowPos(drd16(0x53e0), 0, (int16_t)(uint16_t)(L16(-0xc) + 0x1e), (int16_t)L16(-0xa),
                                (int16_t)(uint16_t)(L16(-8) - L16(-0xc) - 0x54), (int16_t)(uint16_t)(L16(-6) - L16(-0xa)), 4));
  (void)API_SP(14, SetWindowPos(drd16(0x4da4), 0, (int16_t)(uint16_t)(L16(-8) - 0x32), (int16_t)L16(-0xa), 0, 0, 5));
  L16(-0xa) = (uint16_t)(L16(-0xa) + drd16(0x2686) + 3);
  (void)API_SP(14, SetWindowPos(drd16(0x59e6), 0, (int16_t)L16(-0xc), (int16_t)L16(-0xa), (int16_t)(uint16_t)(L16(-8) - L16(-0xc)),
                                (int16_t)(uint16_t)(L16(-6) - L16(-0xa)), 4));
  goto Ldef;

L031e: /* WM_PAINT */
  pushw(0);
  pushw(0x4b64);
  si = API_SP(6, BeginPaint(hwnd, bpfp(-0x44)));
  popw(2);
  (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, si, 0x4b64, 0);
  L32(-0xc) = SetTextColor(si, drd32(0x2870));
  L32(-0x10) = SetBkColor(si, drd32(0x253a));
  L16(-2) = (uint16_t)(drd16(0x4b68) - drd16(0x4b64) - 0x30);
  L16(-6) = ds_strlen(0x4a66);
  if ((int16_t)L16(-6) > 0) {
    L16(-0x12) = si;
    si = L16(-6);
    di = L16(-2);
    for (;;) {
      uint16_t w = (uint16_t)GetTextExtent(L16(-0x12), dsfp(0x4a66), (int16_t)si);
      if (!((int16_t)w > (int16_t)di)) break;
      si = (uint16_t)(si - 1);
      if (!((int16_t)si > 0)) break;
    }
    L16(-6) = si;
    si = L16(-0x12);
  }
  (void)CALL_ORIG(8, 0x9318, FAR_CDECL, 4, si, 0x4b64, 0x4a66, L16(-6));
  {
    const uint16_t a[2] = {si, 0x4b64};
    (void)call6(0x6fc6, a, 2);
  }
  L32(-0x1c) = drd32(0x2532);
  L32(-0x18) = drd32(0x2536);
  di = (uint16_t)(L16(-0x18) - L16(-0x1c));
  L16(-2) = (uint16_t)(drd16(0x2538) - L16(-0x1a));
  L16(-4) = SelectObject(si, drd16(0x0016));
  (void)PatBlt(si, (int16_t)L16(-0x1c), (int16_t)L16(-0x1a), (int16_t)di, 1, 0x00f00021);
  (void)PatBlt(si, (int16_t)L16(-0x1c), (int16_t)L16(-0x1a), 1, (int16_t)L16(-2), 0x00f00021);
  (void)SelectObject(si, drd16(0x0014));
  (void)PatBlt(si, (int16_t)L16(-0x1c), (int16_t)(uint16_t)(L16(-0x16) - 1), (int16_t)di, 1, 0x00f00021);
  (void)PatBlt(si, (int16_t)(uint16_t)(L16(-0x18) - 1), (int16_t)L16(-0x1a), 1, (int16_t)L16(-2), 0x00f00021);
  (void)SelectObject(si, L16(-4));
  (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, bpoff(-0x1c), 0xfffc, 0xfffc);
  if ((drd8(0x0791) & 1) != 0) goto L0742;
  if ((int16_t)drd16(0x0ab4) > (int16_t)drd16(0x0ab6)) goto L05c0;
  {
    /* past the messages: the comment DS:0xab6 - DS:0xab4 of the chain DS:0xabc */
    L16(-0x12) = si;
    L32(-4) = drd32(0x0abc);
    uint16_t bx = L16(-4), dx = L16(-2);
    L16(-0x14) = drd16(0x0ab4);
    if ((int16_t)L16(-0x14) < (int16_t)drd16(0x0ab6)) {
      do {
        uint32_t nx = rd32(dx, bx);
        bx = (uint16_t)nx;
        dx = (uint16_t)(nx >> 16);
        L16(-2) = dx;
        L16(-0x14) = (uint16_t)(L16(-0x14) + 1);
      } while ((int16_t)L16(-0x14) < (int16_t)drd16(0x0ab6));
      L16(-4) = bx;
    }
    uint16_t nseg = L16(-2), noff = L16(-4);
    pushd(0x000a000d);
    uint16_t nm = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, rd16(nseg, (uint16_t)(noff + 4)), 1, 1, 1, 0, 0);
    pushd(dsfp(nm));
    L16(-6) = (uint16_t)_wsprintf(drd32(0x53ea), dsfp(0x0b12), MK_FP(rt_ss, rt_sp));
    popw(4);
    nseg = L16(-2);
    noff = L16(-4);
    if (rd16(nseg, (uint16_t)(noff + 6)) == 0) {
      L16(-8) = 0x0b20;
    } else {
      L16(-8) = (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, (uint16_t)(rd16(nseg, (uint16_t)(noff + 6)) - 1), 1, 1, 1, 0, 0);
    }
    pushd(0x000a000d);
    pushd(dsfp(L16(-8)));
    L16(-6) = (uint16_t)(L16(-6) + (uint16_t)_wsprintf(MK_FP(drd16(0x53ec), (uint16_t)(drd16(0x53ea) + L16(-6))), dsfp(0x0b2a),
                                                    MK_FP(rt_ss, rt_sp)));
    popw(4);
    nseg = L16(-2);
    noff = L16(-4);
    if ((int16_t)rd16(nseg, (uint16_t)(noff + 0x0a)) >= 0) {
      L16(-0x14) = 0x03e8;
      (void)CALL_ORIG(8, 0xa666, FAR_CDECL, 6, (uint16_t)(noff + 0x0c), nseg, rd16(nseg, (uint16_t)(noff + 0x0a)),
                      (uint16_t)(drd16(0x53ea) + L16(-6)), drd16(0x53ec), bpoff(-0x14));
    } else {
      uint16_t dseg = drd16(0x53ec), doff = (uint16_t)(drd16(0x53ea) + L16(-6));
      uint16_t s = (uint16_t)(noff + 0x0c), k = 0;
      for (;;) {
        uint8_t c = rd8(nseg, (uint16_t)(s + k));
        wr8(dseg, (uint16_t)(doff + k), c);
        if (c == 0) break;
        k = (uint16_t)(k + 1);
      }
    }
    L32(-4) = drd32(0x53ea);
    si = L16(-0x12);
    goto L061b;
  }
L05c0:
  {
    uint16_t v = call6_1(0x7646, drd16(0x0ab6));
    di = v;
    if ((int16_t)drd16(0x0ab6) < 0 && (int16_t)drd16(0x0ab4) > 0) {
      ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x22);
    } else if ((int16_t)drd16(0x0ab6) >= 0 && filter_bit(di) != 0 && drd16(0x0aba) == 0) {
      ax = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x21);
    } else {
      ax = call6_1(0x774a, drd16(0x0ab6));
    }
    L16(-4) = ax;
    L16(-2) = MEM_SEL_DGROUP;
  }
L061b:
  (void)SetTextColor(si, 0x00ffffff);
  L16(-6) = (uint16_t)SetBkMode(si, 1);
  {
    bool mark = false;
    if ((int16_t)drd16(0x0ab6) < 0 && (int16_t)drd16(0x0ab4) > 0)
      mark = true;
    else if ((int16_t)drd16(0x0ab6) >= 0 && (int16_t)drd16(0x0ab4) > (int16_t)drd16(0x0ab6) && shown_filtered() != 0)
      mark = true;
    if (mark) (void)CALL_ORIG(8, 0x9482, FAR_CDECL, 4, si, bpoff(-0x1c), 0x0b36, 8);
  }
  (void)SetTextColor(si, drd32(0x2870));
  L32(-0x24) = L32(-0x1c);
  L32(-0x20) = L32(-0x18);
  {
    uint16_t tseg = L16(-2), toff = L16(-4), n = 0;
    while (rd8(tseg, (uint16_t)(toff + n)) != 0) n = (uint16_t)(n + 1);
    (void)DrawText(si, L32(-4), (int16_t)n, bpfp(-0x24), 0x0c10);
  }
  if (!((int16_t)L16(-0x1e) > (int16_t)L16(-0x16))) {
    (void)ShowWindow(drd16(0x54fe), 0);
    uint16_t tseg = L16(-2), toff = L16(-4), n = 0;
    while (rd8(tseg, (uint16_t)(toff + n)) != 0) n = (uint16_t)(n + 1);
    (void)DrawText(si, L32(-4), (int16_t)n, bpfp(-0x1c), 0x0810);
  } else {
    SetWindowText(drd16(0x54fe), L32(-4));
    (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, bpoff(-0x1c), 4, 4);
    (void)API_SP(14, SetWindowPos(drd16(0x54fe), 0, (int16_t)L16(-0x1c), (int16_t)L16(-0x1a), (int16_t)(uint16_t)(L16(-0x18) - L16(-0x1c)),
                                  (int16_t)(uint16_t)(L16(-0x16) - L16(-0x1a)), 0x44));
  }
  (void)SetBkMode(si, (int16_t)L16(-6));
  goto L0777;
L0742:
  di = (uint16_t)SetBkMode(si, 1);
  (void)SetTextColor(si, drd32(0x2870));
  (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, si, (uint16_t)(L16(-0x1c) + 0x1a), L16(-0x1a), 0x0b3f, 3, 0);
  (void)SetBkMode(si, (int16_t)di);
L0777:
  (void)SetTextColor(si, L32(-0xc));
  (void)SetBkColor(si, L32(-0x10));
  EndPaint(hwnd, bpfp(-0x44));
  goto L0e59;

L079c: /* WM_ERASEBKGND */
  GetClientRect(hwnd, bpfp(-8));
  (void)FillRect(wParam, bpfp(-8), drd16(0x0010));
  goto L07ba;

L07c6: /* WM_CTLCOLOR */
  if ((uint16_t)lParam != drd16(0x54fe)) goto Ldef;
  (void)SetBkColor(wParam, drd32(0x253a));
  ret = drd16(0x0010);
  goto out;

L07e8: /* WM_SETCURSOR */
  GetCursorPos(bpfp(-4));
  ScreenToClient(hwnd, bpfp(-4));
  {
    const uint16_t a[2] = {L16(-4), L16(-2)};
    if ((uint16_t)call6(0x7294, a, 2) == 0) goto Ldef;
  }
  (void)SetCursor(drd16(0x5442));
  goto L07ba;

L081c: /* WM_GETMINMAXINFO */
  {
    uint16_t seg = (uint16_t)(lParam >> 16), off = (uint16_t)lParam;
    wr16(seg, (uint16_t)(off + 0x0c), (uint16_t)((uint16_t)(drd16(0x266c) + 0x63) * 2));
    uint16_t v = drd16(0x267a);
    uint16_t t = (uint16_t)((uint16_t)((uint16_t)(v + v + v) << 2) + v);
    wr16(seg, (uint16_t)(off + 0x0e), (uint16_t)((uint16_t)((int16_t)t >> 1) + 0x16));
  }
  goto Ldef;

L0846: /* WM_KEYDOWN */
  if (wParam == 0x28) goto L09b2;
  if (wParam == 0x26) goto L094f;
  if ((drd8(0x0791) & 1) != 0) goto Ldef;
  if (wParam == 0x24) {
    dwr16(0x0ab6, 0xffff);
    goto L09b2;
  }
  if (wParam != 0x23) goto L0e59;
  dwr16(0x0ab6, (uint16_t)(drd16(0x0ab4) + drd16(0x0ac4)));
  goto L094f;

L0882: /* WM_CHAR */
  if (wParam == 0x0d) goto L0a50;
  if (wParam == 0x2b) goto L0dea;
  if (wParam != 0x2d) goto L0e59;
  si = 0;
  while (si < 0x31 && (drd8((uint16_t)(si + 0x534e)) & drd8((uint16_t)(si + 0x5508))) == 0) si++;
  if (si == 0x31) goto L0e59;
L08b3:
  dwr16(0x0aba, drd16(0x0aba) == 0 ? 1 : 0);
  if ((int16_t)drd16(0x0ab6) >= 0) {
    uint16_t v = call6_1(0x7646, drd16(0x0ab6));
    L16(-0x4a) = (uint16_t)((v & 0xff00) | drd8((uint16_t)((uint16_t)(int16_t)((int16_t)v >> 3) + 0x5508)));
    uint16_t v2 = call6_1(0x7646, drd16(0x0ab6));
    uint8_t cl = (uint8_t)(L8(-0x4a) & (uint8_t)(1u << (v2 & 7)));
    uint16_t a = cl == 0 ? 0 : 1;
    if (a == drd16(0x0aba)) goto L0e41;
  }
  L16(-0x14) = call6_1(0x6ef4, drd16(0x0aba));
  if (L16(-0x14) == 0xffff) L16(-0x14) = call6_1(0x6f66, drd16(0x0aba));
  dwr16(0x0ab6, L16(-0x14));
  goto L0e41;

L092e: /* WM_COMMAND */
  L16(-0x48) = (uint16_t)(lParam >> 16);
  if (L16(-0x48) == 0) (void)SetFocus(drd16(0x2806));
  if ((uint16_t)lParam == drd16(0x4d9e) && L16(-0x48) == 0) goto L094f;
  if ((uint16_t)lParam == drd16(0x4da2) && L16(-0x48) == 0) goto L09b2;
  if ((uint16_t)lParam == drd16(0x4da4) && L16(-0x48) == 0) {
    (void)call6_1(0x8640, 0x03e8);
    goto L0a0a;
  }
  if ((uint16_t)lParam != drd16(0x4da0) || L16(-0x48) != 0) goto Ldef;
  goto L0a50;

L094f: /* previous */
  if ((drd8(0x0791) & 1) != 0) {
    (void)call6_1(0x8640, 0xffff);
    goto L0a0a;
  }
  if ((GetAsyncKeyState(0x10) & 0xfffe) != 0) {
    dwr16(0x0ab6, 0xffff);
    L16(-0x14) = call6_1(0x6ef4, 0);
  } else {
    L16(-0x14) = call6_1(0x6f66, 0);
  }
  if (L16(-0x14) != 0xffff) goto L0987;
  if ((uint16_t)(drd16(0x0ab4) + drd16(0x0ac4)) != drd16(0x0ab6)) goto L0e59;
  dwr16(0x0ab6, (uint16_t)(drd16(0x0ab6) - 1));
  goto L0a0a;
L0987:
  dwr16(0x0ab6, L16(-0x14));
  goto L0a0a;

L09b2: /* next */
  if ((drd8(0x0791) & 1) != 0) {
    (void)call6_1(0x8640, 1);
    goto L0a0a;
  }
  if ((GetAsyncKeyState(0x10) & 0xfffe) != 0) {
    dwr16(0x0ab6, (uint16_t)(drd16(0x0ab4) + drd16(0x0ac4)));
    L16(-0x14) = call6_1(0x6f66, 0);
  } else {
    L16(-0x14) = call6_1(0x6ef4, 0);
  }
  if (L16(-0x14) == 0xffff) goto L0e59;
  goto L0987;

L0a0a:
  dwr8(0x0791, (uint8_t)(drd8(0x0791) & 0xef));
  (void)call6_1(0x6a68, hwnd);
  InvalidateRect(hwnd, dsfp(0x2532), 1);
L0a27:
  if ((drd8(0x0791) & 8) == 0) goto L0e59;
  dwr8(0x53f0, (uint8_t)(drd8(0x53f0) | 4));
  (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
  goto L0e59;

L0a50: /* goto */
  if ((drd8(0x0791) & 1) != 0) goto L0d04;
  if ((int16_t)drd16(0x0ab4) <= (int16_t)drd16(0x0ab6)) goto L0d04;
  switch (drd16(0x415c)) {
    case 1:
      (void)CALL_ORIG(9, 0x2ed8, FAR_CDECL, 2, 0, drd16(0x4160));
      UpdateWindow(drd16(0x0150));
      (void)API_SP(10, SendMessage(drd16(0x0150), 0x0102, 0x0076, 0));
      si = call6_1(0x7646, drd16(0x0ab6));
      if (si == 0x3e || si == 0x3f || ((int16_t)si >= 0xaf && (int16_t)si <= 0xb4)) {
        if ((drd8(0x0791) & 0x10) == 0) goto L0ae3;
        if (drd16(0x4b74) == 1 && drd16(0x4160) == drd16(0x4b78)) (void)rt_call_far(MK_FP(SEG_SEL(26), 0x0000), FAR_CDECL, (const uint16_t[]){0}, 1);
      }
      goto L0a27;
    case 2:
      (void)CALL_ORIG(10, 0x26b2, FAR_CDECL, 2, 0, drd16(0x4160));
      UpdateWindow(drd16(0x0150));
      (void)API_SP(10, SendMessage(drd16(0x0150), 0x0102, 0x0076, 0));
      goto L0a27;
    case 3:
      (void)PostMessage(drd16(0x2806), 0x0111, 0x007e, 0);
      goto L0a27;
    case 4: {
      uint8_t cl = (uint8_t)(drd8(0x4161) & 0x0f);
      dwr16(0x252a, (uint16_t)(1u << cl));
      dwr16(0x252c, (uint16_t)((drd8(0x252d) << 8) | drd8(0x4160)));
      const uint16_t a[1] = {0x252a};
      (void)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, a, 1);
      if (drd16(0x014e) != 0) {
        InvalidateRect(drd16(0x5658), 0, 1);
      } else {
        dwr16(0x0d3e, 1);
        (void)PostMessage(drd16(0x2806), 0x0111, 0x0100, 0);
      }
      goto L0a27;
    }
    case 5:
      (void)PostMessage(drd16(0x2806), 0x0111, 0x007d, 0);
      goto L0a27;
    case 6:
      (void)CALL_ORIG(7, 0x063e, FAR_CDECL, 1, 0x4156);
      if ((drd8(0x0791) & 0x10) == 0) goto L0ae3;
      (void)CALL_ORIG(29, 0x0000, FAR_CDECL, 1, drd16(0x4160));
      goto L0a27;
    case 7:
      (void)PostMessage(drd16(0x2806), 0x0111, 0x07de, 0);
      goto L0a27;
    case 8:
      (void)PostMessage(drd16(0x2806), 0x0111, 0x005f, 0);
      goto L0a27;
    case 9: {
      dwr8(0x588e, 2);
      uint32_t pi = MakeProcInstance(MK_FP(SEG_SEL(6), 0x7ed0), drd16(0x54f4));
      L16(-4) = (uint16_t)(pi >> 16);
      uint16_t cx = drd16(0x0312) != 0 ? drd16(0x0312) : drd16(0x2806);
      L16(-2) = (uint16_t)API_SP(12, DialogBox(drd16(0x54f4), 0x00000056, cx, pi));
      FreeProcInstance(pi);
      if (L16(-2) == 0) goto L0a27;
      if ((uint16_t)CALL_ORIG(7, 0x3ef4, FAR_CDECL, 2, 0x5866, bpoff(-4)) != 0) {
        dwr32(0x0872, L32(-4));
      } else if (drd32(0x0872) != 0) {
        goto L0a27;
      }
      for (uint16_t k = 0; k < 11; k++) dwr8((uint16_t)(0x5382 + k), drd8((uint16_t)(0x564c + k)));
      goto L0a27;
    }
    case 10: {
      uint32_t r = CALL_ORIG(7, 0x01a0, FAR_CDECL, 1, drd16(0x4156));
      L16(-2) = (uint16_t)(r >> 16);
      if (r == 0) goto L0a27;
      L32(-0x24) = rd32((uint16_t)(r >> 16), (uint16_t)((uint16_t)r + 2));
      L16(-0x20) = 8;
      (void)CALL_ORIG(11, 0x62f2, FAR_CDECL, 2, bpoff(-0x24), 0);
      (void)CALL_ORIG(11, 0x5142, FAR_CDECL, 3, L16(-0x24), L16(-0x22), 1);
      goto L0a27;
    }
    case 11:
      if (drd16(0x177e) != 0 && drd16(0x1766) == 0x16f0) goto L0a27;
      (void)PostMessage(drd16(0x2806), 0x0111, 0x0901, 0);
      goto L0a27;
    default:
      goto L0a27;
  }
L0ae3:
  dwr8(0x0791, (uint8_t)(drd8(0x0791) | 0x10));
  (void)call6_1(0x6a68, hwnd);
  goto L0a27;

L0cd6: /* WM_LBUTTONDOWN, WM_LBUTTONDBLCLK */
  L16(-0xc) = (uint16_t)lParam;
  L16(-0xa) = (uint16_t)(lParam >> 16);
  {
    const uint16_t a[2] = {L16(-0xc), (uint16_t)(lParam >> 16)};
    si = (uint16_t)call6(0x7294, a, 2);
  }
  if (si == 1) goto L0dea;
  if (si == 2) goto L08b3;
  if (si != 3) goto L0e59;
L0d04: /* the comment mode on / off */
  if ((drd8(0x0791) & 1) != 0) {
    (void)call6_1(0x8640, 0);
    goto L0dac;
  }
  if ((int16_t)drd16(0x0ab4) > (int16_t)drd16(0x0ab6)) {
    dwr16(0x0ac8, 0);
    goto L0dac;
  }
  {
    di = drd16(0x0abc);
    L16(-6) = drd16(0x0abe);
    L16(-0x14) = (uint16_t)(drd16(0x0ab6) - drd16(0x0ab4));
    for (;;) {
      uint16_t a = L16(-0x14);
      L16(-0x14) = (uint16_t)(L16(-0x14) - 1);
      if (a == 0) break;
      uint32_t nx = rd32(L16(-6), di);
      di = (uint16_t)nx;
      L16(-6) = (uint16_t)(nx >> 16);
    }
    uint16_t bx = drd16(0x0ac0);
    L16(-2) = drd16(0x0ac2);
    dwr16(0x0ab8, 0);
    if ((L16(-2) | bx) != 0) {
      L16(-8) = di;
      uint16_t ds = L16(-2);
      for (;;) {
        if ((uint16_t)(rd16(ds, (uint16_t)(bx + 6)) - rd16(L16(-6), (uint16_t)(di + 4)) - 1) == 0 &&
            rd16(ds, (uint16_t)(bx + 8)) == drd16(0x0ab6))
          break;
        uint32_t nx = rd32(ds, bx);
        bx = (uint16_t)nx;
        ds = (uint16_t)(nx >> 16);
        dwr16(0x0ab8, (uint16_t)(drd16(0x0ab8) + 1));
        if ((ds | bx) == 0) break;
      }
    }
    dwr16(0x0ac8, (uint16_t)(rd16(L16(-6), (uint16_t)(di + 4)) + 1));
  }
L0dac:
  {
    uint16_t a = (drd8(0x0791) & 1) == 0 ? 1 : 0;
    uint16_t w = (uint16_t)(((uint16_t)(a << 8) ^ ((uint16_t)drd8(0x0791) << 8)) & 0x0100);
    dwr16(0x0790, (uint16_t)(drd16(0x0790) ^ w));
  }
  InvalidateRect(drd16(0x0158), 0, 1);
  (void)call6_1(0x6a68, hwnd);
  (void)SetFocus(drd16(0x59e6));
  goto L0e59;

L0dea: /* toggle the filter of the shown message */
  if ((int16_t)drd16(0x0ab6) < 0) goto L0e59;
  {
    uint16_t v = call6_1(0x7646, drd16(0x0ab6));
    si = v;
    uint16_t on = filter_bit(v) == 0 ? 1 : 0;
    const uint16_t a[2] = {si, on};
    (void)call6(0x8960, a, 2);
  }
  (void)CALL_ORIG(9, 0x641c, FAR_CDECL, 1, 1);
  if ((drd8(0x0791) & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
L0e41:
  InvalidateRect(drd16(0x0158), 0, 1);
  (void)call6_1(0x6a68, hwnd);
L0e59:
  ret = 0;
  goto out;
L07ba:
  ret = 1;
  goto out;
Ldef:
  ret = API_SP(10, DefWindowProc(hwnd, msg, wParam, lParam));
out:
  FRAME_LEAVE();
  return ret;
}
