/* Decompiled code segment 20 (NE segment 20, selector 0x00A0).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg20.c; Ghidra's pseudo-C in re/decomp/seg20 is only a reading aid.
 *
 * Segment 20 is the code of one kind of computer player: the one that 17:0x0000 (ai_player_turn)
 * starts when the player word DS:0x5a6a + 0xc0 * player shifted right by 13 is 5. It is called
 * "type-5" here (ai5_), a placeholder in the same sense as "type-2" / "type-7" in segments 17 and 19:
 * which computer player kind it is has not been established. The functions:
 *   20:0x0000  ai5_turn: the turn of the type-5 player
 *   20:0x1da0  ai5_create_designs: queue the ship designs whose techs the player has reached
 *   20:0x2286  ai5_send_fleet_to_best_planet: send a fleet to the best-scoring planet within range
 *   20:0x23ba  ai5_choose_target_planet: choose the planet a fleet should move to (planet id or -1)
 *   20:0x2998  ai5_order_fleet: give a fleet with waypoints its next order
 *   20:0x2cbc  ai5_fleet_strong_enough: sum up a fleet's ships and compare with the strength limit
 * Names: Ghidra's are all generic (FUN_1098_<off>), no function has an export name, so the C names
 * describe what each function does (CONVENTIONS rule f); they are descriptive, not the game's. The
 * old fn_20_<off> aliases are kept in the comment above each function.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with n the ENTER
 * size and one push for each PUSH SI / PUSH DI; the stack slots whose address a callee gets (order records, out
 * words, buffers) and the slots 20:0x0000 shares between its phases are BP_LOCAL slots at the original offsets, the
 * other locals are C locals. Memory is read and written at the original's widths and in its order; an offset is a
 * uint16_t and wraps like the original's. Near calls inside the segment are PUSH CS; CALL near, i.e. far calls with a
 * 4-byte return address: a callee that mirrors a frame is reached through rt_call_far, a leaf by its C name. The
 * callees in other segments that are not decompiled are reached with CALL_ORIG. No function of this segment calls a
 * Win16 API. */
#include <stddef.h>
#include <string.h>

#include "src/stars.h"

/* Data access: the globals of src/globals.h by name, the records of src/stars.h (struct Fleet, Planet, Waypoint,
 * WaypointListHdr, ProdQueueHdr, ProdItem, ShipDesign, Player) through FAR_REC, and the static tables of DGROUP
 * through design_slot() (DS_DEFAULT_SHIP_DESIGNS), player_row() (DS_PLAYERS), planet_xy_at() (DS_PLANET_XY) and
 * tech_level(). What is still spelled by offset: DS:0x5392 (only g_DAT_1110_5392 in src/globals.h), the six tech
 * level bytes (no field in struct Player), the dword at fleet + 0x68 that chains fleets (struct Fleet calls it
 * unknown_68) and the part-list offsets in this segment's code. Control flow: the original's branch ladders are
 * written as loops and if/else; where a callee's answer or a random draw decides, the draws are made in the
 * original's order (every RAND is a call of 8:0x8730 and part of the diff). */

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
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }

/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
#define WD(seg, base, d, v) wr32((seg), (uint16_t)((base) + (d)), (uint32_t)(v))
/* A far pointer global (offset word first) as its two words. */
#define FAR_WORDS(g) (*(struct FarPtrWords *)&(g))
/* The info array (DS:0x1600 = g_planet_info_array): 16 bytes per planet id; INFO(idx16, d) is the offset of byte d of slot idx16/16. */
#define INFO_SEG g_planet_info_array_segment
#define INFO(idx16, d) ((uint16_t)(g_planet_info_array + (uint16_t)((idx16)) + (d)))
/* The x | y << 16 point of the planet whose id << 2 is `id4`: the dword at DS:0x31ba + id4 (g_planet_xy_table). */
static inline uint32_t planet_xy_at(uint16_t id4) { return drd32((uint16_t)(id4 + DS_PLANET_XY)); }
/* The offset of a field of a record of src/stars.h (checked against the original's offset by STATIC_OFFSET there). */
#define FLD(type, f) ((uint16_t)offsetof(type, f))
/* Records of src/stars.h at a far address, and the static tables of DGROUP by index. */
#define FLEET(seg, off) FAR_REC(struct Fleet, (seg), (off))
#define PLANET(seg, off) FAR_REC(struct Planet, (seg), (off))
static inline struct ShipDesign *design_slot(uint16_t k)
{
  return FAR_REC(struct ShipDesign, MEM_SEL_DGROUP, DS_DEFAULT_SHIP_DESIGNS + (uint16_t)(k * sizeof(struct ShipDesign)));
}
static inline struct Player *player_row(uint16_t player)
{
  return FAR_REC(struct Player, MEM_SEL_DGROUP, DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)));
}
/* The player's six tech levels: signed bytes at DS_PLAYERS + 0xc0 * player + 0x1a + field (2:0x587a, src/seg02.c,
 * reads them at DS:0x5a7e + 0xc0 * player; src/stars.h has no field for them yet). */
static inline int8_t tech_level(uint16_t player, uint16_t field)
{
  return (int8_t)drd8((uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)) + 0x1a + field));
}
/* A far-call helper: rt_call_far with the word arguments in declaration order (CDECL, far). */
static uint32_t far_call(uint16_t seg, uint16_t off, const uint16_t *args, int16_t n)
{
  return rt_call_far(MK_FP(SEG_SEL(seg), off), FAR_CDECL, args, n);
}
/* 8:0xbc50(a, b): the squared distance of two map points (dwords x | y << 16), DX:AX. */
#define DIST2(a, b) CALL_ORIG(8, 0xbc50, FAR_CDECL, 4, (uint16_t)(a), (uint16_t)((a) >> 16), (uint16_t)(b), (uint16_t)((b) >> 16))
/* A random number below n from the game's generator, 8:0x8730(n). */
#define RAND(n) ((uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, (n)))

/* The code segment's data at 20:0x1d59 to 20:0x1d9f: the words 0x1d59 + 2 * k are offsets (plus 0x1c61) of
 * the part lists that 18:0x0000 reads through CS, the 8 bytes from 20:0x1d97 are the table that
 * 20:0x0000 passes to 18:0x28fc as a far pointer. */
#define CS_TAB_BASE 0x1d59
static const uint8_t cs_tab[0x47] = {
  0x00, 0x00, 0x07, 0x00, 0x0e, 0x00, 0x15, 0x00, 0x1c, 0x00, 0x23, 0x00, 0x2a, 0x00, 0x31, 0x00, 0x38, 0x00,
  0x3f, 0x00, 0x46, 0x00, 0x4a, 0x00, 0x55, 0x00, 0x60, 0x00, 0x6b, 0x00, 0x76, 0x00, 0x81, 0x00, 0x8c, 0x00,
  0x97, 0x00, 0xa2, 0x00, 0xad, 0x00, 0xaf, 0x00, 0xb5, 0x00, 0xbb, 0x00, 0xbf, 0x00, 0xc2, 0x00, 0xc9, 0x00,
  0xd0, 0x00, 0xd7, 0x00, 0xde, 0x00, 0xeb, 0x00, 0x03, 0x42, 0x14, 0x71, 0x54, 0x34, 0x77, 0x37, 0x90,
};
/* The word at 20:`off` (MOV AX, CS:[off]); the callers' offsets stay inside the table. */
static inline uint16_t cs_word(uint16_t off) {
  uint16_t i = (uint16_t)(off - CS_TAB_BASE);
  return (uint16_t)(cs_tab[i] | (cs_tab[i + 1] << 8));
}
/* The design call 18:0x0000(slot | kind << 16, 20:(0x1c61 + the word at 20:cs_off) as the part list). */
#define MSG(lo, hi, cs_off) \
  (uint16_t)CALL_ORIG(18, 0x0000, FAR_CDECL, 4, (lo), (hi), (uint16_t)(cs_word(cs_off) + 0x1c61), SEG_SEL(20))

/* ai5_fleet_strong_enough: 20:0x2cbc (Ghidra FUN_1098_2cbc, alias fn_20_2cbc), 176 bytes. ENTER 8,0;
 * PUSH DI; PUSH SI (a leaf: no callee, no stack address given away). FAR CDECL (fleet [BP+6] far
 * pointer, out [BP+0xA] a near pointer into DGROUP or 0), AX = 0 or 1.
 * Adds up the fleet's ship counts: the counts of designs 2 to 4 once and those of designs 5 to 7 twice
 * (strength = the 16-bit sum). When the strength limit g_ai_threshold_0 (a byte, signed compare) is
 * above it, adds twice the counts of designs 8 and 9 whose design record (DS_DEFAULT_SHIP_DESIGNS, slots 8
 * and 9) has hull 9 and bit 1 of its flags clear, and answers 0 if the limit is still above the sum.
 * Otherwise stores the strength through `out` (when not null) and answers 1. */
DECOMP(20, 0x2cbc, ai5_fleet_strong_enough, FAR_CDECL)
uint16_t ai5_fleet_strong_enough(uint32_t fleet, uint16_t out)
{
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  uint16_t strength = 0, total;
  for (uint16_t design = 2; design < 5; design++) strength = (uint16_t)(strength + FLEET(fseg, foff)->ship_count[design]);
  for (uint16_t design = 5; design < 8; design++) {
    uint16_t count = FLEET(fseg, foff)->ship_count[design];
    strength = (uint16_t)(strength + (uint16_t)(count + count));
  }
  total = strength;
  if ((int16_t)g_ai_threshold_0 > (int16_t)strength) {
    for (uint16_t design = 8; design < 10; design++) {
      uint16_t count = FLEET(fseg, foff)->ship_count[design];
      if (count != 0 && (design_slot(design)->flags & 2) == 0 && design_slot(design)->hull == 9)
        total = (uint16_t)(total + (uint16_t)(count + count));
    }
    if (!((int16_t)g_ai_threshold_0 < (int16_t)total)) return 0;
  }
  if (out != 0) NEAR_AT(uint16_ua, out) = total;
  return 1;
}

/* ai5_send_fleet_to_best_planet: 20:0x2286 (Ghidra FUN_1098_2286, alias fn_20_2286), 308 bytes.
 * ENTER 0x22,0; PUSH DI; PUSH SI. FAR CDECL (fleet [BP+6]), AX = the answer of 18:0x2664 or 0.
 * Walks the object pointer list (g_object_ptr_list, g_object_ptr_count entries, stops at a null entry);
 * each entry is a planet. A planet whose squared distance (8:0xbc50) from the fleet's position is below
 * 0x1440 gets a score: 8 * byte +9 + 10 * byte +0xa + 7 * byte +0xb (the three bytes mineral_concentration_maybe[]). The score of
 * the fleet's current planet is kept as `current_score`, the best score and its planet as `best_score` and
 * `best_planet`. When there is a best planet and 6 * current_score / 5 (16-bit signed) is below its score,
 * builds the order record at BP-0x22 (the planet's xy from DS_PLANET_XY, its id, kind 1, flags) and hands it
 * to 18:0x2664(fleet, record, 0). The record is only partly written (the rest is stack junk the callee sees). */
DECOMP(20, 0x2286, ai5_send_fleet_to_best_planet, FAR_CDECL)
uint16_t ai5_send_fleet_to_best_planet(uint32_t fleet)
{
  FRAME_ENTER_REGS(0x22, 2);
#define ORDER BP_LOCAL(struct Waypoint, -0x22)
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  uint32_t best_planet = 0;
  uint16_t current_score = 0, best_score = 0;
  uint16_t count = 0;
  for (; (int16_t)g_object_ptr_count > (int16_t)count; count++) {
    const uint32_t planet = *FAR_REC(uint32_ua, FAR_WORDS(g_object_ptr_list).seg,
                                     (uint16_t)(FAR_WORDS(g_object_ptr_list).off + (uint16_t)(count * sizeof(FARPTR))));
    const uint16_t pseg = FP_SEG(planet), poff = FP_OFF(planet);
    if (planet == 0) break;
    const uint32_t dist2 = DIST2(FLEET(fseg, foff)->xy, planet_xy_at((uint16_t)(PLANET(pseg, poff)->id << 2)));
    if (!((int32_t)dist2 < 0x1440)) continue;
    const uint16_t score = (uint16_t)((uint16_t)(PLANET(pseg, poff)->mineral_concentration_maybe[0] << 3) +
                                      (uint16_t)(PLANET(pseg, poff)->mineral_concentration_maybe[2] * 7) +
                                      (uint16_t)(PLANET(pseg, poff)->mineral_concentration_maybe[1] * 10));
    if (FLEET(fseg, foff)->planet_id == PLANET(pseg, poff)->id) current_score = score;
    if ((int16_t)best_score < (int16_t)score) {
      best_planet = planet;
      best_score = score;
    }
  }
  if (best_planet == 0 || (int16_t)(uint16_t)(6 * current_score) / 5 >= (int16_t)best_score) {
    FRAME_LEAVE();
    return 0;
  }
  {
    const uint16_t bseg = FP_SEG(best_planet), boff = FP_OFF(best_planet);
    ORDER.target_id = PLANET(bseg, boff)->id;
    ORDER.flags = (uint16_t)((ORDER.flags & 0xf1ff) | 0x0100); /* kind 1: a planet (bits 9 to 11 of the flags word) */
    const uint32_t xy = planet_xy_at((uint16_t)(PLANET(bseg, boff)->id << 2));
    ORDER.x = (uint16_t)xy;
    ORDER.y = (uint16_t)(xy >> 16);
    ORDER.flags = (uint16_t)(((ORDER.flags & 0xfff0) | 0x1000) & 0xff6f) | 0x0060; /* low nibble 0, bit 12 set, bits 5 and 6 set */
  }
  {
    const uint16_t result = (uint16_t)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, foff, fseg, (uint16_t)(rt_bp - 0x22), 0);
    FRAME_LEAVE();
    return result;
  }
#undef ORDER
}

/* The dword at fleet + 0x68 that chains fleets (no field in struct Fleet: unknown_68). */
enum { FLEET_CHAIN_LINK = 0x68 };
/* Waypoint list entry kinds and the cursor (g_waypoint_table: a WaypointListHdr and Waypoint entries). */
#define FL(f) FLEET(FP_SEG(f), FP_OFF(f))
#define PL(p) PLANET(FP_SEG(p), FP_OFF(p))
#define CURSOR_HDR FAR_REC(struct WaypointListHdr, g_waypoint_table_segment_maybe, g_waypoint_table)
#define CURSOR_WP0 FAR_REC(struct Waypoint, g_waypoint_table_segment_maybe, (uint16_t)(g_waypoint_table + sizeof(struct WaypointListHdr)))

static uint32_t fleet_ptr(uint16_t i)
{
  return *FAR_REC(uint32_ua, FAR_WORDS(g_fleet_table).seg, (uint16_t)(FAR_WORDS(g_fleet_table).off + (uint16_t)(i * sizeof(FARPTR))));
}
/* Entry n of the fleet's waypoint list (a WaypointListHdr, then Waypoint entries). */
static struct Waypoint *waypoint_of(uint32_t fleet, uint16_t n)
{
  const uint32_t list = FL(fleet)->waypoints;
  return FAR_REC(struct Waypoint, FP_SEG(list), (uint16_t)(FP_OFF(list) + sizeof(struct WaypointListHdr) + n * sizeof(struct Waypoint)));
}
/* The kind nibble (the low nibble of the flags' high byte) of waypoint n. */
static uint16_t waypoint_kind(uint32_t fleet, uint16_t n) { return (uint16_t)((waypoint_of(fleet, n)->flags >> 8) & 0xf); }
static bool fleet_has_far_waypoint_kind(uint32_t fleet, uint16_t kind)
{
  return (int16_t)FL(fleet)->waypoint_count > 1 && waypoint_kind(fleet, 1) == kind;
}
/* The 16-bit offset arithmetic of `ship_count[index]` for an index that may be 0xffff. */
static uint16_t fleet_ships(uint32_t fleet, uint16_t index)
{
  return rd16(FP_SEG(fleet), (uint16_t)(FP_OFF(fleet) + FLD(struct Fleet, ship_count) + (uint16_t)(2 * index)));
}
/* The dword of the ship counts of designs `design` and design + 1. */
static uint32_t fleet_ship_pair(uint32_t fleet, uint16_t design)
{
  return *FAR_REC(uint32_ua, FP_SEG(fleet), (uint16_t)(FP_OFF(fleet) + FLD(struct Fleet, ship_count) + 2 * design));
}
static uint8_t *planet_info_byte(uint16_t planet_id, uint16_t d)
{
  return FAR_REC(uint8_t, INFO_SEG, INFO((uint16_t)(planet_id << 4), d));
}

/* Production list (g_production_list): is there an entry for which match() answers true? */
static bool match_finished_ship(const struct ProdItem *it) { return (it->w1 & 0xe) == 4 && (it->w1 & 1) == 0 && (it->w0 & 0xfc00) < 0x4000; }
static bool match_item_3800(const struct ProdItem *it)
{
  return (it->w1 & 0xe) == 2 && (it->w1 & 1) == 0 && (it->w0 & 0xfc00) >= 0x3800 && (it->w0 & 0xfc00) <= 0x4400;
}
static bool match_item_3400(const struct ProdItem *it) { return (it->w1 & 0xe) == 2 && (it->w0 & 0xfc00) == 0x3400 && (it->w1 & 1) == 0; }
static bool production_list_has(bool (*match)(const struct ProdItem *))
{
  for (uint16_t i = 0;; i++) {
    const uint16_t seg = FAR_WORDS(g_production_list).seg, off = FAR_WORDS(g_production_list).off;
    if (!((int16_t)(uint16_t)FAR_REC(struct ProdQueueHdr, seg, off)->count > (int16_t)i)) return false;
    if (match(FAR_REC(struct ProdItem, seg, (uint16_t)(off + sizeof(struct ProdQueueHdr) + i * sizeof(struct ProdItem))))) return true;
  }
}

/* Selects the fleet as the current map object and flags the cursor waypoint list (9:0x2a74(2, id), count := 1). */
static void select_fleet_for_waypoints(uint32_t fleet)
{
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FL(fleet)->id);
  g_waypoint_count_maybe = 1;
  CURSOR_HDR->count = 1;
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
}
/* Selects the fleet and sets bits of the cursor's first waypoint (flags := (flags & keep) | bits, after the select),
 * then 7:0x153e(0xffff, 0x4b8c). `extra` also sets its two words fld_08 and fld_0a to 5. */
static void select_fleet_with_order(uint32_t fleet, uint16_t keep, uint16_t bits, bool extra)
{
  (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FL(fleet)->id);
  CURSOR_WP0->flags = (uint16_t)((CURSOR_WP0->flags & keep) | bits);
  if (extra) {
    CURSOR_WP0->fld_08 = 5;
    CURSOR_WP0->fld_0a = 5;
  }
  (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
}


/* The far pointer to entry `i` of the object pointer list (g_object_ptr_list): a planet record, or 0 at the end. */
static uint32_t object_ptr(uint16_t i)
{
  return *FAR_REC(uint32_ua, FAR_WORDS(g_object_ptr_list).seg,
                  (uint16_t)(FAR_WORDS(g_object_ptr_list).off + (uint16_t)(i * sizeof(FARPTR))));
}

/* Clears byte d of every slot of the planet info array. */
static void clear_info_bytes(uint16_t d)
{
  for (uint16_t k = 0; (int16_t)k < (int16_t)g_planet_count_in_game; k++)
    wr8(g_planet_info_array_segment, (uint16_t)(g_planet_info_array + d + 0x10 * k), 0);
}
/* Commits the production queue edited for the planet (26:0x0810). */
static void flush_queue(uint16_t accepted) { (void)far_call(26, 0x0810, &accepted, 1); }
/* Sum of the planet's three bytes mineral_concentration_maybe[]. */
static uint16_t planet_byte_sum(uint32_t planet)
{
  uint16_t sum = 0;
  for (uint16_t k = 0; (int16_t)k < 3; k++) sum = (uint16_t)(sum + PL(planet)->mineral_concentration_maybe[k]);
  return sum;
}

/* ai5_order_fleet: 20:0x2998 (Ghidra FUN_1098_2998, alias fn_20_2998), 804 bytes. ENTER 0x1c,0; PUSH DI;
 * PUSH SI. FAR CDECL (fleet [BP+6]), no result. Decides what a fleet that already has waypoints does
 * next and hands it an order record (18 bytes at BP-0x1c, for 18:0x2664(fleet, record, 0)):
 *  1. With more than one waypoint it copies the waypoint list entry 1 into the record. When the squared distance
 *     (8:0xbc50) from the fleet's position to the record's position is above 0xf424, or the record's kind (the low
 *     nibble of the flags' high byte) is not 2, then: kind 2 stops here, kind 1 looks up the planet
 *     7:0x01e4(record id) and stops unless it is unowned or the player's own and not flagged (flags bit 1) and last
 *     updated this turn (planet.turn = g_current_turn); other kinds go on.
 *  2. ai5_fleet_strong_enough gives the fleet's strength (BP-0xa); `ships` = its counts of designs 8 and 9. Sets bit 7
 *     of the fleet's flags, selects it (9:0x2a74(2, id)). A fleet without a current planet goes to
 *     18:0x457a(fleet, 0x1c2) and ends.
 *  3. Otherwise looks up its planet (7:0x01e4(planet_id)). Whether the fleet may stay (the result `stay`), or looks for
 *     a new target by 18:0x39f6 (`score`) or 18:0x3922 (`nearby`), depends on the planet's owner, the strength limits
 *     g_ai_threshold_0 to 3 and random draws (8:0x8730(10)). Targets come from 18:0x39f6 (two tries, the first only
 *     with g_game_flags bit 4), 18:0x3922 or 18:0x3854; the chosen planet (or its xy) fills the record (kind bits 1 or
 *     2, flag bits) and 18:0x2664 queues it. The info slot byte +0xa of a chosen planet gets bit 7. The record is only
 *     partly written (the rest is stack junk the callee sees). */
DECOMP(20, 0x2998, ai5_order_fleet, FAR_CDECL)
void ai5_order_fleet(uint32_t fleet)
{
  FRAME_ENTER_REGS(0x1c, 2);
#define ORDER BP_LOCAL(struct Waypoint, -0x1c)
#define STRENGTH L16(-0xa) /* left by 18:0x2cbc; stack junk when that answers 0 */
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  enum { STAY, SCORE, NEARBY } route = STAY;
  uint32_t planet, target = 0;
  uint16_t ships;
  do { /* leaves with `break` where the original returns */
    if ((int16_t)FLEET(fseg, foff)->waypoint_count > 1) {
      memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x1c))), waypoint_of(fleet, 1), sizeof(struct Waypoint));
      const uint16_t kind = (uint16_t)((ORDER.flags >> 8) & 0xf);
      const bool far_away = (int32_t)DIST2(FLEET(fseg, foff)->xy, *(uint32_ua *)&ORDER) > 0xf424;
      if (!(far_away && kind == 2)) {
        if (kind == 2) break;
        if (kind == 1) {
          planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, ORDER.target_id);
          if (planet == 0) break;
          if (PLANET(FP_SEG(planet), FP_OFF(planet))->owner != 0xffff &&
              (PLANET(FP_SEG(planet), FP_OFF(planet))->owner != g_current_player ||
               (PLANET(FP_SEG(planet), FP_OFF(planet))->flags & 2) != 0))
            break;
          if (PLANET(FP_SEG(planet), FP_OFF(planet))->turn != g_current_turn) break;
        }
      }
    }
    (void)ai5_fleet_strong_enough(fleet, (uint16_t)(rt_bp - 0xa));
    ships = (uint16_t)(FLEET(fseg, foff)->ship_count[8] + FLEET(fseg, foff)->ship_count[9]);
    FLEET(fseg, foff)->flags = (uint8_t)(FLEET(fseg, foff)->flags | 0x80);
    (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FLEET(fseg, foff)->id);
    if (FLEET(fseg, foff)->planet_id == 0xffff) {
      (void)CALL_ORIG(18, 0x457a, FAR_CDECL, 3, foff, fseg, 0x1c2);
      break;
    }
    planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, FLEET(fseg, foff)->planet_id);
    if (PLANET(FP_SEG(planet), FP_OFF(planet))->owner == g_current_player) {
      /* the fleet sits at a planet of the player: should it move on, and by which search? */
      route = STAY;
      do {
        if ((int16_t)g_ai_threshold_0 <= (int16_t)STRENGTH && (int16_t)g_ai_threshold_2 <= (int16_t)ships) {
          route = SCORE;
          break;
        }
        if ((uint8_t)(player_row(g_current_player)->status_flags & 0x1c) <= 4) break;
        if (!((int16_t)(uint16_t)(g_ai_threshold_0 * 2) < (int16_t)STRENGTH) && (int16_t)STRENGTH < 0x3c) break;
        if ((int16_t)RAND(10) < 5) {
          route = SCORE;
          break;
        }
        if ((int16_t)(uint16_t)(g_ai_threshold_0 * 3) < (int16_t)STRENGTH && (int16_t)RAND(10) < 7) {
          route = SCORE;
          break;
        }
        if ((int16_t)STRENGTH <= 0x78) break;
        if ((int16_t)RAND(10) < 7) route = SCORE;
      } while (0);
    } else if ((int16_t)g_ai_threshold_1 <= (int16_t)STRENGTH && (int16_t)g_ai_threshold_3 <= (int16_t)ships) {
      /* a planet of another player or none: only an unowned one is worth a visit */
      route = PLANET(FP_SEG(planet), FP_OFF(planet))->owner == 0xffff ? SCORE : STAY;
    } else {
      (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, foff, fseg, 0);
      route = NEARBY;
      if ((uint8_t)(player_row(g_current_player)->status_flags & 0x1c) > 4) {
        if ((int16_t)(uint16_t)(g_ai_threshold_0 * 2) < (int16_t)STRENGTH && (int16_t)RAND(10) < 5) route = SCORE;
        else if ((int16_t)(uint16_t)(g_ai_threshold_0 << 2) < (int16_t)STRENGTH && (int16_t)RAND(10) < 7) route = SCORE;
        else if ((int16_t)STRENGTH > 0x78 && (int16_t)RAND(10) < 7) route = SCORE;
      }
    }
    if (route == STAY) break;
    if (route == SCORE) {
      if ((g_game_flags & 0x10) != 0)
        target = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, FP_OFF(planet), FP_SEG(planet), 0x2252, SEG_SEL(17));
      if (target == 0) target = CALL_ORIG(18, 0x39f6, FAR_CDECL, 4, FP_OFF(planet), FP_SEG(planet), 0x216c, SEG_SEL(17));
    } else {
      target = CALL_ORIG(18, 0x3922, FAR_CDECL, 4, FP_OFF(planet), FP_SEG(planet), 0x3be2, SEG_SEL(18));
    }
    if (target != 0) {
      const uint16_t tseg = FP_SEG(target), toff = FP_OFF(target);
      *planet_info_byte(PLANET(tseg, toff)->id, 0xa) |= 0x80;
      ORDER.target_id = PLANET(tseg, toff)->id;
      ORDER.flags = (uint16_t)((ORDER.flags & 0xf1ff) | 0x0100);
      const uint32_t xy = planet_xy_at((uint16_t)(PLANET(tseg, toff)->id << 2));
      ORDER.x = (uint16_t)xy;
      ORDER.y = (uint16_t)(xy >> 16);
    } else {
      /* 18:0x3854 answers a record with id at +0 and position at +8 (the layout of struct Fleet) */
      target = CALL_ORIG(18, 0x3854, FAR_CDECL, 4, foff, fseg, 0x2154, SEG_SEL(17));
      if (target == 0) break;
      ORDER.target_id = FLEET(FP_SEG(target), FP_OFF(target))->id;
      ORDER.flags = (uint16_t)((ORDER.flags & 0xf2ff) | 0x0200);
      ORDER.x = FLEET(FP_SEG(target), FP_OFF(target))->x;
      ORDER.y = FLEET(FP_SEG(target), FP_OFF(target))->y;
    }
    ORDER.flags = (uint16_t)(((ORDER.flags & 0xfff0) | 0x1000) & 0xff4f) | 0x0040; /* low nibble 0, bit 12 set, bit 6 set */
    (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, foff, fseg, (uint16_t)(rt_bp - 0x1c), 0);
  } while (0);
  FRAME_LEAVE();
#undef STRENGTH
#undef ORDER
}

/* ai5_choose_target_planet: 20:0x23ba (Ghidra FUN_1098_23ba, alias fn_20_23ba), 1502 bytes.
 * ENTER 0x38,0; PUSH DI; PUSH SI. FAR CDECL (fleet [BP+6]), AX = the id of the planet the fleet was
 * sent to, or 0xffff for none. In this order:
 *  1. A fleet at a planet of the player (fleet.planet_id != 0xffff, 7:0x01e4) whose 9:0x45aa rating is above 0x19 and
 *     whose resource surface_amounts_maybe[3] is at least 1000 "loads" cargo: the cargo room left is 10:0x292a(fleet, 2) minus the
 *     three cargo dwords, at most a twentieth of surface_amounts_maybe[3]. The planet's surface_amounts_maybe[3] is lowered by that amount and put
 *     back around two calls of 9:0x4de8, to measure how much the player's rating of it changes. Over the object
 *     pointer list the planet within squared distance 0x9c40 whose info slot bit 0 of byte +0xe is clear and that
 *     gains most from the amount (the weights 3, 6, 9 and 12 percent by distance 0x9c4, 0x2710, 0x57e4) is the target
 *     when the gain is large enough (against thresholds by the turn): the fleet is selected, 18:0x0750 (kind 3) and
 *     7:0x153e are queued and that slot's bit is set.
 *  2. Otherwise, a fleet at some planet whose first resource (surface_amounts_maybe[kind]) of 2500 or more is `kind`: the planet
 *     within range with the smallest value of that resource below 1000 is the target when it is below 200; the cargo
 *     room (at most a fifth of the resource) is queued with 18:0x0750 (kind `kind`) and 7:0x153e.
 *  3. Otherwise the planet within range with the best 9:0x45aa rating, lowered by 2, 4 or 6 for distances above 50, 100
 *     and 150 (squared 0x9c4, 0x2710, 0x57e4).
 * The target becomes an order record (18 bytes at BP-0x30, planet xy and id, kind 1, flag nibbles 2 in four words) for
 * 18:0x2664(fleet, record, 0); the answer is its id when that call answers non-zero, else 0xffff. The record is
 * zero-filled first and only partly written. */
DECOMP(20, 0x23ba, ai5_choose_target_planet, FAR_CDECL)
uint16_t ai5_choose_target_planet(uint32_t fleet)
{
  FRAME_ENTER_REGS(0x38, 2);
#define ORDER BP_LOCAL(struct Waypoint, -0x30)
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  uint32_t here = 0, target = 0;
  uint16_t answer = 0xffff;
  bool found = false;
  if (FLEET(fseg, foff)->planet_id != 0xffff) {
    here = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, FLEET(fseg, foff)->planet_id);
    /* part 1: load cargo at one of the player's rich planets */
    do {
      if (here == 0 || PLANET(FP_SEG(here), FP_OFF(here))->owner != g_current_player) break;
      const uint16_t hseg = FP_SEG(here), hoff = FP_OFF(here);
      if ((int16_t)CALL_ORIG(9, 0x45aa, FAR_CDECL, 2, hoff, hseg) <= 0x19) break;
      if ((int32_t)PLANET(hseg, hoff)->surface_amounts_maybe[3] < 0x3e8) break;
      uint32_t amount = CALL_ORIG(10, 0x292a, FAR_CDECL, 3, foff, fseg, 2);
      for (uint16_t k = 0; k < 3; k++) amount = amount - FLEET(fseg, foff)->cargo[k];
      const uint32_t share = (uint32_t)((int32_t)PLANET(hseg, hoff)->surface_amounts_maybe[3] / 0x14);
      if ((int32_t)share < (int32_t)amount) amount = share;
      if ((int32_t)amount <= 0) break;
      /* the rating change that giving the amount away would bring: the planet's value is lowered and put back */
      uint16_t base = (uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, hoff, hseg, g_current_player);
      PLANET(hseg, hoff)->surface_amounts_maybe[3] = PLANET(hseg, hoff)->surface_amounts_maybe[3] - amount;
      base = (uint16_t)(base - (uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, hoff, hseg, g_current_player));
      PLANET(hseg, hoff)->surface_amounts_maybe[3] = PLANET(hseg, hoff)->surface_amounts_maybe[3] + amount;
      uint16_t best_gain = 0;
      for (uint16_t i = 0; (int16_t)i < (int16_t)g_object_ptr_count; i++) {
        const uint32_t planet = object_ptr(i);
        const uint16_t pseg = FP_SEG(planet), poff = FP_OFF(planet);
        if (planet == 0) break;
        if (planet == here) continue;
        if ((*planet_info_byte(PLANET(pseg, poff)->id, 0xe) & 1) != 0) continue;
        const int32_t dist2 = (int32_t)DIST2(FLEET(fseg, foff)->xy, planet_xy_at((uint16_t)(PLANET(pseg, poff)->id << 2)));
        if (dist2 > 0x9c40) continue;
        const int16_t percent = dist2 > 0x57e4 ? 12 : dist2 > 0x2710 ? 9 : dist2 > 0x9c4 ? 6 : 3;
        const uint32_t gain = (uint32_t)((int32_t)((uint32_t)(int32_t)percent * amount) / -100) + amount;
        PLANET(pseg, poff)->surface_amounts_maybe[3] = PLANET(pseg, poff)->surface_amounts_maybe[3] + gain;
        uint16_t rating = (uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, poff, pseg, g_current_player);
        PLANET(pseg, poff)->surface_amounts_maybe[3] = PLANET(pseg, poff)->surface_amounts_maybe[3] - gain;
        rating = (uint16_t)(rating - (uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, poff, pseg, g_current_player));
        if ((int16_t)rating > (int16_t)best_gain) {
          best_gain = rating;
          target = planet;
        }
      }
      if (target == 0) break;
      if ((int16_t)(uint16_t)(base + 5) > (int16_t)best_gain) break;
      if (!((int16_t)(uint16_t)(base + 0xa) <= (int16_t)best_gain) && g_current_turn > 0x50) break;
      if (!((int16_t)(uint16_t)(base + 0xf) <= (int16_t)best_gain) && g_current_turn > 0xa0) break;
      (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FLEET(fseg, foff)->id);
      (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, FLEET(fseg, foff)->planet_id, 2, FLEET(fseg, foff)->id, 3, (uint16_t)amount);
      (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, FLEET(fseg, foff)->id, 0x4b8c);
      *planet_info_byte(PLANET(FP_SEG(target), FP_OFF(target))->id, 0xe) |= 1;
      found = true;
    } while (0);
    /* part 2: the planet has a resource of 2500 or more; look for a poor neighbour */
    do {
      if (found || here == 0) break;
      const uint16_t hseg = FP_SEG(here), hoff = FP_OFF(here);
      uint16_t kind = 0;
      while (kind <= 2 && (int32_t)PLANET(hseg, hoff)->surface_amounts_maybe[kind] < 0x9c4) kind++;
      if ((int16_t)kind > 2) break;
      int32_t poorest = 1000;
      target = 0;
      for (uint16_t i = 0; (int16_t)i < (int16_t)g_object_ptr_count; i++) {
        const uint32_t planet = object_ptr(i);
        const uint16_t pseg = FP_SEG(planet), poff = FP_OFF(planet);
        if (planet == 0) break;
        if (planet == here) continue;
        const int32_t dist2 = (int32_t)DIST2(FLEET(fseg, foff)->xy, planet_xy_at((uint16_t)(PLANET(pseg, poff)->id << 2)));
        if (dist2 > 0x9c40) continue;
        const int32_t value = (int32_t)PLANET(pseg, poff)->surface_amounts_maybe[kind];
        if (value >= poorest) continue;
        poorest = value;
        target = planet;
      }
      if (target == 0 || poorest >= 0xc8) break;
      uint32_t amount = CALL_ORIG(10, 0x292a, FAR_CDECL, 3, foff, fseg, 2);
      for (uint16_t k = 0; k < 3; k++) amount = amount - FLEET(fseg, foff)->cargo[k];
      const uint32_t share = (uint32_t)((int32_t)PLANET(hseg, hoff)->surface_amounts_maybe[kind] / 5);
      if ((int32_t)share < (int32_t)amount) amount = share;
      if ((int32_t)amount > 0) (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FLEET(fseg, foff)->id);
      (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, FLEET(fseg, foff)->planet_id, 2, FLEET(fseg, foff)->id, kind, (uint16_t)amount);
      (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, FLEET(fseg, foff)->id, 0x4b8c);
      found = true;
    } while (0);
  }
  if (!found) { /* part 3: the best rated planet within range */
    int16_t best_rating = 0;
    target = 0;
    for (uint16_t i = 0; (int16_t)i < (int16_t)g_object_ptr_count; i++) {
      const uint32_t planet = object_ptr(i);
      if (planet == 0) break;
      if (planet == here) continue;
      const int32_t dist2 = (int32_t)DIST2(FLEET(fseg, foff)->xy, planet_xy_at((uint16_t)(PLANET(FP_SEG(planet), FP_OFF(planet))->id << 2)));
      if (dist2 > 0x9c40) continue;
      uint16_t rating = (uint16_t)CALL_ORIG(9, 0x45aa, FAR_CDECL, 2, FP_OFF(planet), FP_SEG(planet));
      if (dist2 > 0x57e4) rating = (uint16_t)(rating - 6);
      else if (dist2 > 0x2710) rating = (uint16_t)(rating - 4);
      else if (dist2 > 0x9c4) rating = (uint16_t)(rating - 2);
      if (best_rating < (int16_t)rating) {
        best_rating = (int16_t)rating;
        target = planet;
      }
    }
    found = target != 0;
  }
  if (found) {
    const uint16_t tseg = FP_SEG(target), toff = FP_OFF(target);
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x30))), 0, sizeof(struct Waypoint));
    ORDER.flags = (uint16_t)((ORDER.flags & 0xf1ff) | 0x0100);
    {
      const uint32_t xy = planet_xy_at((uint16_t)(PLANET(tseg, toff)->id << 2));
      ORDER.x = (uint16_t)xy;
      ORDER.y = (uint16_t)(xy >> 16);
    }
    ORDER.target_id = PLANET(tseg, toff)->id;
    ORDER.flags = (uint16_t)((ORDER.flags & 0xfff1) | 1);
    ORDER.flags = (uint16_t)(ORDER.flags | 0x1000);
    ORDER.flags = (uint16_t)((ORDER.flags & 0xff4f) | 0x40);
    for (uint16_t k = 0; k < 4; k++) /* the words at record offsets 8 to 0xe get 2 in their top nibble */
      BP_LOCAL(uint16_t, -0x28 + 2 * k) = (uint16_t)((BP_LOCAL(uint16_t, -0x28 + 2 * k) & 0x0fff) | 0x2000);
    if ((uint16_t)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, foff, fseg, (uint16_t)(rt_bp - 0x30), 0) != 0)
      answer = PLANET(tseg, toff)->id;
  }
  FRAME_LEAVE();
  return answer;
#undef ORDER
}

/* Copies the design record `src` (DS_DEFAULT_SHIP_DESIGNS slot) to the local buffer at BP+`buf`, sets bit 1 of its
 * flags and passes it to 18:0x0630(buffer, slot). */
static void ai5_redesign_from(uint16_t src, uint16_t slot, int16_t buf)
{
  memcpy(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp + buf))), design_slot(src), sizeof(struct ShipDesign));
  BP_LOCAL(struct ShipDesign, buf).flags = (uint8_t)(BP_LOCAL(struct ShipDesign, buf).flags | 2);
  (void)CALL_ORIG(18, 0x0630, FAR_CDECL, 2, (uint16_t)(rt_bp + buf), slot);
}

/* ai5_create_designs: 20:0x1da0 (Ghidra FUN_1098_1da0, alias fn_20_1da0), 1254 bytes. ENTER 0xa0,0;
 * PUSH DI; PUSH SI. FAR CDECL, no arguments, no result. The type-5 player's new ship designs: for the
 * slots of the design record array (DS_DEFAULT_SHIP_DESIGNS) whose flags have bit 1 set (and further
 * conditions on the turn, the previous slot's stamp word turn_stamp_maybe and the player's tech levels) it asks
 * 18:0x0000(slot | kind << 16, part list in this segment's code, see cs_tab) to build a design; several slots
 * try up to 5 random part lists (8:0x8730) until 18:0x0000 answers non-zero. Slots 0, 1 and 2 are first copied to
 * the local buffer BP-0x9e with bit 1 of the flags set and given to 18:0x0630 (with the slot number).
 * 2:0x50d8 is asked once (kind 1, index 0xf, record BP-0x10); the local buffer's address is visible to 18:0x0630. */
DECOMP(20, 0x1da0, ai5_create_designs, FAR_CDECL)
void ai5_create_designs(void)
{
  FRAME_ENTER_REGS(0xa0, 2);
  /* slots 14 and 15: the second one is skipped for a veteran (status bits >= 8) with a low tech level 3 */
  for (uint16_t slot = 14; slot <= 15; slot++) {
    if ((design_slot(slot)->flags & 2) == 0) continue;
    const uint16_t veteran = (uint8_t)(player_row(g_current_player)->status_flags & 0x1c) >= 8 ? 1 : 0;
    if (veteran != 0 && slot == 15 && tech_level(g_current_player, 3) < 0x0f) continue;
    if (MSG(slot, veteran == 0 ? 0x17 : 0x18, (uint16_t)(0x1d83 + 2 * veteran)) == 0 && slot == 14) {
      if (veteran != 0) (void)MSG(slot, 0x16, 0x1d85);
      else (void)MSG(slot, 0x15, 0x1d87);
    }
  }
  if ((design_slot(12)->flags & 2) != 0 && tech_level(g_current_player, 1) >= 5 && tech_level(g_current_player, 2) >= 6 && MSG(0x0c, 0x1d, 0x1d95) == 0) {
    for (uint16_t tries = 0; (int16_t)tries < 5; tries++)
      if (MSG(0x0c, 6, (uint16_t)(0x1d59 + 2 * RAND(4))) != 0) break;
  }
  if ((design_slot(13)->flags & 2) != 0 && tech_level(g_current_player, 1) >= 10 && tech_level(g_current_player, 2) >= 9 && MSG(0x0d, 0x1d, 0x1d95) == 0) {
    for (uint16_t tries = 0; (int16_t)tries < 5; tries++)
      if (MSG(0x0d, 6, (uint16_t)(0x1d61 + 2 * RAND(4))) != 0) break;
  }
  if ((design_slot(10)->flags & 2) != 0 && MSG(0x0a, 2, 0x1d89) == 0) (void)MSG(0x0a, 1, 0x1d89);
  if ((design_slot(11)->flags & 2) != 0) (void)MSG(0x0b, 2, 0x1d89);
  if (g_current_turn < 0x14 && (design_slot(2)->flags & 2) == 0 && design_slot(2)->build_progress_maybe == 0) ai5_redesign_from(2, 2, -0x9e);
  for (uint16_t slot = 2; slot <= 4; slot++) {
    if ((design_slot(slot)->flags & 2) == 0) continue;
    if (slot != 2 && (uint16_t)(g_current_turn - design_slot(slot - 1)->turn_stamp_maybe) <= 0x14) continue;
    for (uint16_t tries = 0; (int16_t)tries < 5; tries++)
      if (MSG(slot, 7, (uint16_t)(0x1d8b + 2 * RAND(4))) != 0) break;
  }
  if (g_current_turn < 0x28 && (design_slot(7)->flags & 2) != 0) (void)MSG(7, 0x0f, 0x1d81);
  {
    const uint16_t a[] = {(uint16_t)(rt_bp - 0x10), 1, 0x0f};
    if ((uint16_t)far_call(2, 0x50d8, a, 3) == 1 && (design_slot(1)->flags & 2) == 0 && design_slot(1)->build_progress_maybe == 0 &&
        design_slot(1)->slots[0].item != 0x0f) {
      ai5_redesign_from(1, 1, -0x9e);
      (void)MSG(1, 0x0f, 0x1d81);
    }
  }
  for (uint16_t slot = 5; slot <= 7; slot++) {
    if ((design_slot(slot)->flags & 2) == 0) continue;
    if (slot != 5 && ((design_slot(slot - 1)->flags & 2) != 0 || (uint16_t)(g_current_turn - design_slot(slot - 1)->turn_stamp_maybe) <= 0x14))
      continue;
    uint16_t first_list = slot == 5 ? 0x0b : 0x0f;
    if (slot == 7) first_list = (uint16_t)(((RAND(2) == 0) ? 4 : 0) + 0x0b);
    if (RAND(3) == 0 || MSG(slot, 0x1d, 0x1d93) == 0) {
      for (uint16_t tries = 0; (int16_t)tries < 5; tries++)
        if (MSG(slot, 9, (uint16_t)(0x1d59 + 2 * (uint16_t)(RAND(4) + first_list))) != 0) break;
    }
  }
  for (uint16_t slot = 8; slot <= 9; slot++) {
    if ((design_slot(slot)->flags & 2) == 0 || tech_level(g_current_player, 1) < 0x0e) continue;
    if (slot != 8 && ((design_slot(slot - 1)->flags & 2) != 0 || (uint16_t)(g_current_turn - design_slot(slot - 1)->turn_stamp_maybe) <= 0x0f))
      continue;
    if (MSG(slot, 9, 0x1d7f) == 0) (void)MSG(slot, 0x13, (uint16_t)(0x1d59 + 2 * (slot == 8 ? 8 : 9)));
  }
  if (design_slot(0)->hull != 5 && (uint8_t)(player_row(g_current_player)->status_flags & 0x1c) > 4 && design_slot(0)->build_progress_maybe == 0 &&
      tech_level(g_current_player, 5) >= 4 && tech_level(g_current_player, 4) >= 5 && tech_level(g_current_player, 3) >= 6 && tech_level(g_current_player, 2) >= 6 && tech_level(g_current_player, 0) >= 6) {
    ai5_redesign_from(0, 0, -0x9e);
    (void)MSG(0, 5, 0x1d6d);
  }
  FRAME_LEAVE();
}

/* ---- 20:0x0000 ai5_turn ---------------------------------------------------------------------------------------
 * The frame of ai5_turn (ENTER 0x14e) keeps the slots at the original BP offsets: callees see the ones whose address
 * they get (the order record, the out words of 18:0x5d3c, 18:0x2ba6, 18:0x4912, 9:0x4f76, 18:0x3558 / 0x3600 / 0x098a
 * and 2:0x50d8's request record). Names say what a slot holds; the CHOICE_xx slots are the answers of 18:0x5d3c(xx, ...)
 * (a design slot number or 0xffff). */
#define SAVED_STATE L16(-0x70)    /* the answer of 18:0x28fc, given back to 18:0x59ba */
#define PLAYER_LEVEL L16(-0x6c)   /* bits 2 to 4 of the player's status_flags */
#define SLOT7_CLAIMED L16(-0x84)  /* 1 when the player still has to finish design slot 7 */
#define DESIGN_BOUND L16(-0x86)   /* 7 - SLOT7_CLAIMED */
#define BUDGET L16(-0x90)         /* 0x32, 0x46 or 0x64 by the turn */
#define BUDGET_ANSWER L16(-0x8e)  /* the answer of the first 18:0x5d3c */
#define WANTED(i) L8(-0x60 + (i)) /* 16 bytes, one per design slot, filled by 18:0x5d3c and 18:0x5bae */
#define SCRATCH(i) L8(-0x6a + (i)) /* 10 bytes for 18:0x5bae / 18:0x4912 / 18:0x2ba6 (DS:0x5392 points at them) */
#define CHOICE_0C L16(-0x94)
#define CHOICE_0E L16(-0x9c)
#define CHOICE_0A L16(-0x96)
#define CHOICE_08 L16(-0x92)
#define CHOICE_02 L16(-0x98)
#define CHOICE_05 L16(-0x9a)
#define DESIGN_NO L16(-0x9e)      /* the design number (1 or 7) of the production items 18:0x2732 gets */
#define N_D14_15 L16(-0xa0)       /* the player's fleets that hold ships of designs 14 and 15 */
#define N_D0_START L16(-0xa2)     /* N_D0 as counted before the planets are walked */
#define N_D0 L16(-0xa4)           /* fleets with ships of design 0 */
#define N_D10_11 L16(-0xa6)
#define N_D12_13 L16(-0xa8)
#define N_D2_9 L16(-0xaa)         /* fleets with ships of any of the designs 2 to 9 */
#define QUOTA L16(-0xac)          /* planets of the player / 20, at most 10 (when 2:0x50d8(1, 0xd) allows it) */
#define QUEUE_CHANGED L16(-0x82)  /* 1 once a production item was queued for the current planet */
#define STAT_7A L16(-0x7a)        /* a word 18:0x2ba6 writes; raised by the queued items */
#define STAT_80 L16(-0x80)        /* the answer of 18:0x2ba6 */
#define RATING_GAIN L16(-0x7c)    /* 9:0x4de8(planet, player) of the current planet */
#define LOW_THIRD L16(-0x7e)      /* 1 when the third resource is the first of the planet below 5000 */
#define OWN_CHAIN L32(-0x40)      /* the player's fleets chained through the dword at fleet + 0x68 */
#define OTHER_CHAIN L32(-0x44)    /* the other players' fleets, chained the same way */
#define OUT_72 L16(-0x72)         /* the out word of 9:0x4f76 */
#define ORDER_13C BP_LOCAL(struct Waypoint, -0x13c)

/* ai5_turn: 20:0x0000 (Ghidra FUN_1098_0000, alias fn_20_0000), 7265 bytes. ENTER 0x14e,0; PUSH DI;
 * PUSH SI. FAR CDECL (turn_arg at [BP+6], passed on to 18:0x59ba and 26:0x0100). No result.
 * The whole turn of the type-5 computer player (started by 17:0x0000 ai_player_turn). In the original's order:
 *  1. 18:0x28fc saves state; the strength limits g_ai_threshold_0 to 3 are set from the turn; the designs wanted
 *     (18:0x5d3c, 18:0x5bae) and design slots 7, 14 and 15 are prepared; 20:0x1da0 creates the other designs.
 *  2. The player's fleets are counted by their ship designs (the N_ slots).
 *  3. For each planet that can build the production items are queued (18:0x2732) by the counts and by random draws
 *     (8:0x8730), and the queue is committed (26:0x0810).
 *  4. Three walks over the fleets give them orders (18:0x5e82, 18:0x3bb4, 18:0x098a, 18:0x0750, 18:0x0864,
 *     18:0x08fe, 18:0x4510, 20:0x2286, 20:0x23ba, 20:0x2998, 18:0x1434).
 *  5. 18:0x59ba restores the state; 21:0x1e2a is called.
 * Where the original returns from a part, the C leaves a `do { } while (0)` block with `break`. The order record at
 * BP-0x13c is only partly written (the rest is stack junk the callee sees). */
DECOMP(20, 0x0000, ai5_turn, FAR_CDECL)
void ai5_turn(uint16_t turn_arg)
{
  FRAME_ENTER_REGS(0x14e, 2);

  /* ---- 1. the strength limits, the budgets and the designs ---- */
  PLAYER_LEVEL = (uint16_t)((player_row(g_current_player)->status_flags & 0x1c) >> 2);
  /* 18:0x28fc(table 20:0x1d97, 8, flag 0xf0000) */
  SAVED_STATE = (uint16_t)CALL_ORIG(18, 0x28fc, FAR_CDECL, 5, 0x1d97, SEG_SEL(20), 8, 0, 0x0f);
  if (g_current_turn < 0x28 || ((design_slot(7)->flags & 2) == 0 && design_slot(7)->hull == 0x0f)) {
    SLOT7_CLAIMED = 1;
    const uint16_t request[] = {(uint16_t)(rt_bp - 0x10), 1, 0x0f};
    if ((uint16_t)far_call(2, 0x50d8, request, 3) == 1 && design_slot(7)->build_progress_maybe == 0) {
      ai5_redesign_from(7, 7, -0x13c);
      SLOT7_CLAIMED = 0;
    }
  } else {
    SLOT7_CLAIMED = 0;
  }
  DESIGN_BOUND = (uint16_t)(7 - SLOT7_CLAIMED);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, SLOT7_CLAIMED != 0 ? 0x37c : 0x3fc);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 1);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0xc000);
  (void)CALL_ORIG(18, 0x36aa, FAR_CDECL, 1, 0x3000);
  {
    /* the strength limits by the turn */
    uint16_t limit = 6;
    if (g_current_turn > 0x82) limit = (uint16_t)((uint16_t)(g_current_turn - 0x78) / 0x14 + 6);
    if ((int16_t)limit > 0x32) limit = 0x32;
    g_ai_threshold_0 = (uint8_t)limit;
    g_ai_threshold_1 = (uint8_t)((uint8_t)limit >> 1);
    limit = 6;
    if (g_current_turn > 0x73) limit = (uint16_t)((uint16_t)(g_current_turn - 0x64) / 0x16 + 6);
    if ((int16_t)limit > 0x0c) limit = 0x0c;
    g_ai_threshold_2 = (uint8_t)limit;
    uint16_t half = (uint16_t)(((int16_t)limit - ((int16_t)limit < 0 ? -1 : 0)) >> 1);
    half = (uint16_t)(half - 1);
    if ((int16_t)half > 3) half = 3;
    g_ai_threshold_3 = (uint8_t)half;
  }
  /* what to build: 18:0x5d3c answers a design slot (or 0xffff) per kind and fills WANTED */
  memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x60))), 0, 16);
  if (g_current_turn < 0x78) BUDGET = 0x32;
  else BUDGET = (g_current_turn < 0xc8) ? 0x46 : 0x64;
  BUDGET_ANSWER = (uint16_t)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0c, 0x0d, BUDGET, (uint16_t)(rt_bp - 0x94), (uint16_t)(rt_bp - 0x60));
  for (uint16_t slot = 12; slot <= 13; slot++)
    if (WANTED(slot) != 0 && (design_slot(slot)->flags & 2) == 0 && design_slot(slot)->hull == 0x1d) WANTED(slot) = 0;
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0e, 0x0f, 0x1388, (uint16_t)(rt_bp - 0x9c), (uint16_t)(rt_bp - 0x60));
  if ((design_slot(15)->flags & 2) == 0) {
    const uint16_t request[] = {(uint16_t)(rt_bp - 0x10), 0x80, 6};
    if ((uint16_t)far_call(2, 0x50d8, request, 3) == 1) {
      int16_t pick = -1;
      if (design_slot(14)->slots[2].item != 6) {
        pick = 14;
      } else if (design_slot(15)->slots[2].item != 6) {
        const uint16_t request15[] = {(uint16_t)(rt_bp - 0x10), 1, 0x0f};
        if ((uint16_t)far_call(2, 0x50d8, request15, 3) == 1 && design_slot(14)->slots[0].item != 0x0f) pick = 15;
      }
      if (pick >= 0) {
        if (design_slot((uint16_t)pick)->build_progress_maybe != 0) WANTED(0x0e) = 3;
        else ai5_redesign_from((uint16_t)pick, (uint16_t)pick, -0x13c);
      }
    }
  }
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x0a, 0x0b, BUDGET, (uint16_t)(rt_bp - 0x96), (uint16_t)(rt_bp - 0x60));
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x08, 0x09, BUDGET, (uint16_t)(rt_bp - 0x92), (uint16_t)(rt_bp - 0x60));
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x02, 0x04, BUDGET, (uint16_t)(rt_bp - 0x98), (uint16_t)(rt_bp - 0x60));
  (void)CALL_ORIG(18, 0x5d3c, FAR_CDECL, 5, 0x05, DESIGN_BOUND, BUDGET, (uint16_t)(rt_bp - 0x9a), (uint16_t)(rt_bp - 0x60));
  if (g_current_turn > 0x50) {
    (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x60));
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x6a))), 0, 10);
    SCRATCH(0) = 2;
    (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x6a));
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x6a))), 0, 10);
    SCRATCH(1) = 2;
    (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x6a));
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x6a))), 0, 10);
    WANTED(5) = 2;
    WANTED(4) = 2;
    (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x6a));
    memset(LIN(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x6a))), 0, 10);
    WANTED(1) = 2;
    WANTED(0) = 2;
    (void)CALL_ORIG(18, 0x5bae, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x6a));
  }
  (void)far_call(20, 0x1da0, NULL, 0);
  (void)CALL_ORIG(18, 0x4912, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x6a));
  dwr16(0x5392, (uint16_t)(rt_bp - 0x6a)); /* DS:0x5392 is only g_DAT_1110_5392 in src/globals.h: no name to use */
  STAT_80 = (uint16_t)CALL_ORIG(18, 0x2ba6, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x7a));
  DESIGN_NO = (design_slot(1)->slots[0].item == 0x0f || (design_slot(7)->flags & 2) != 0 || design_slot(7)->hull != 0x0f) ? 1 : 7;
  if (DESIGN_NO == 1 && SLOT7_CLAIMED != 0 && (uint16_t)(g_current_turn - design_slot(1)->turn_stamp_maybe) > 5) WANTED(7) = 2;

  /* ---- 2. count the player's fleets by their ship designs ---- */
  clear_info_bytes(0xe);
  N_D14_15 = 0;
  N_D12_13 = 0;
  N_D10_11 = 0;
  N_D2_9 = 0;
  N_D0 = 0;
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_fleet_count; i++) {
    const uint32_t fleet = fleet_ptr(i);
    if (fleet == 0) break;
    if (FL(fleet)->owner != g_current_player) continue;
    if (CHOICE_0E != 0xffff && fleet_ship_pair(fleet, 14) != 0) N_D14_15 = (uint16_t)(N_D14_15 + 1);
    if (FL(fleet)->ship_count[0] != 0) N_D0 = (uint16_t)(N_D0 + 1);
    if (CHOICE_0C != 0xffff && fleet_ship_pair(fleet, 12) != 0) N_D12_13 = (uint16_t)(N_D12_13 + 1);
    if (CHOICE_0A != 0xffff && fleet_ship_pair(fleet, 10) != 0) {
      N_D10_11 = (uint16_t)(N_D10_11 + 1);
      /* a fleet in deep space on its way to a planet (far waypoint of kind 1) that is carrying something */
      if (FL(fleet)->planet_id == 0xffff && fleet_has_far_waypoint_kind(fleet, 1) && (int32_t)FL(fleet)->cargo[3] > 0)
        *planet_info_byte(waypoint_of(fleet, 1)->target_id, 0xe) |= 1;
    }
    for (uint16_t design = 2; (int16_t)design <= 9; design++) {
      if (FL(fleet)->ship_count[design] != 0) {
        N_D2_9 = (uint16_t)(N_D2_9 + 1);
        break;
      }
    }
  }
  N_D0_START = N_D0;

  /* ---- 3. the planets ---- */
  clear_info_bytes(0xd);
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_object_ptr_count; i++) {
    const uint32_t planet = object_ptr(i);
    if (planet == 0) break;
    *planet_info_byte(PL(planet)->id, 0xd) = 1;
  }
  QUOTA = 0;
  if (g_current_turn > 0x78) {
    const uint16_t request[] = {(uint16_t)(rt_bp - 0x10), 1, 0x0d};
    if ((uint16_t)far_call(2, 0x50d8, request, 3) == 1) {
      QUOTA = (uint16_t)((int16_t)player_row(g_current_player)->planet_count / 0x14);
      if ((int16_t)QUOTA > 0x0a) QUOTA = 0x0a;
    }
  }
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  /* rate the other players' planets in the info array (+0xa: rank, +9: seen) */
  for (uint16_t off = g_planet_table, end = (uint16_t)(g_planet_table + (uint16_t)(g_planet_count * sizeof(struct Planet))); end > off;
       off = (uint16_t)(off + sizeof(struct Planet))) {
    const uint16_t seg = g_planet_table_segment_maybe;
    if (PLANET(seg, off)->owner == g_current_player || PLANET(seg, off)->owner == 0xffff) continue;
    uint16_t rank = (uint16_t)((uint16_t)(PLANET(seg, off)->population_estimate_maybe & 0x0fff) / 0xfa + 1);
    if ((int16_t)rank > 6) rank = 6;
    if ((PLANET(seg, off)->flags & 2) != 0) rank = (uint16_t)(rank + 1);
    *planet_info_byte(PLANET(seg, off)->id, 0xa) = (uint8_t)rank;
    *planet_info_byte(PLANET(seg, off)->id, 9) = 1;
  }
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_object_ptr_count; i++) {
    const uint32_t planet = object_ptr(i);
    if (planet == 0) break;
    if ((PL(planet)->flags & 2) == 0 || (int32_t)PL(planet)->surface_amounts_maybe[3] < 0xc8) continue;
    const uint16_t kind = (uint16_t)(PL(planet)->starbase & 0xf);
    if (kind == 0 || (kind == 1 && g_current_turn > 0x19)) continue;
    (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 1, PL(planet)->id);
    if (PL(planet)->queue != 0 && FAR_REC(struct ProdQueueHdr, FP_SEG(PL(planet)->queue), FP_OFF(PL(planet)->queue))->count >= 0x18) continue;

    /* the planet can build: queue what the rules ask for, then commit the queue */
    bool flush = true;
    do {
      (void)far_call(26, 0x0100, &turn_arg, 1);
      QUEUE_CHANGED = 0;
      if (production_list_has(match_finished_ship)) break; /* flushes the unchanged queue */
      RATING_GAIN = (uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, FP_OFF(planet), FP_SEG(planet), g_current_player);
      /* a rich planet and a poor neighbour: queue a transfer (a share of the resource) */
      if (g_current_turn > 0x78 &&
          (int16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, FP_OFF(planet), FP_SEG(planet), (uint16_t)(rt_bp - 0x72)) >= 0x0a &&
          (int32_t)PL(planet)->surface_amounts_maybe[3] > 0x2710 && RAND(4) == 0 && !production_list_has(match_item_3800)) {
        const uint16_t start = RAND(3);
        uint16_t index = start;
        while ((int16_t)(uint16_t)(start + 3) > (int16_t)index && (int32_t)PL(planet)->surface_amounts_maybe[(int16_t)index % 3] <= 0x1388) index++;
        if ((uint16_t)(start - index) != 0xfffd) {
          const uint16_t kind3 = (uint16_t)((int16_t)index % 3);
          uint32_t best = 0;
          uint32_t best_value = 0x186a0;
          for (uint16_t k = 0; (int16_t)k < (int16_t)g_object_ptr_count; k++) {
            const uint32_t candidate = object_ptr(k);
            if (candidate == 0) break;
            if ((int32_t)PL(candidate)->surface_amounts_maybe[kind3] >= (int32_t)best_value) continue;
            if ((int16_t)CALL_ORIG(9, 0x4f76, FAR_CDECL, 3, FP_OFF(candidate), FP_SEG(candidate), (uint16_t)(rt_bp - 0x72)) < 0x0a) continue;
            const uint32_t dist2 = DIST2(planet_xy_at((uint16_t)(PL(planet)->id << 2)), planet_xy_at((uint16_t)(PL(candidate)->id << 2)));
            if (!((int32_t)dist2 < 0x16444)) continue;
            best_value = PL(candidate)->surface_amounts_maybe[kind3];
            best = candidate;
          }
          int32_t share = (int32_t)PL(planet)->surface_amounts_maybe[kind3] / 5;
          if (best != 0 && share > (int32_t)best_value) {
            if (share > 0x4e20) share = 0x4e20;
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, (uint16_t)(kind3 + 0x0e), (uint16_t)(share / 100), 1, 1);
            flush_queue(1);
            g_cursor_what_word = (uint16_t)((g_cursor_what_word & 0xdfff) | 0x1c00);
            g_cursor_what_word = (uint16_t)(g_cursor_what_word ^ (uint16_t)(((uint16_t)(PL(best)->id + 1) ^ g_cursor_what_word) & 0x03ff));
            (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, 0xffff, 0x4c08);
            flush = false; /* the planet is done: the queue was committed above */
            break;
          }
        }
      }
      if (CHOICE_0A != 0xffff && (int16_t)N_D10_11 < 0x40) {
        uint16_t quarter = player_row(g_current_player)->planet_count;
        quarter = (uint16_t)((int16_t)(uint16_t)(quarter + ((int16_t)quarter < 0 ? 3 : 0)) >> 2);
        if ((int16_t)quarter > (int16_t)N_D10_11 && RAND(3) == 0) {
          N_D10_11 = (uint16_t)(N_D10_11 + 1);
          (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, CHOICE_0A, 1, 2, 1);
          QUEUE_CHANGED = 1;
        }
      }
      /* the planet's resources are low: a mine or a factory */
      const bool random_gate = STAT_80 == 0 || ((int16_t)STAT_7A > 0x28 && (g_current_turn > 0x78 || (int16_t)STAT_7A > 0x64));
      bool skip_surface = false;
      if (random_gate && (int16_t)RAND(0x64) >= 8) skip_surface = true;
      else if ((int16_t)STAT_7A >= 0x32 && g_current_turn > 0x78) skip_surface = true;
      if (!skip_surface) {
        bool low = false;
        for (uint16_t k = 0; (int16_t)k <= 2; k++)
          if ((int32_t)PL(planet)->surface_amounts_maybe[k] < 0x1e) low = true;
        if (low) break; /* the planet is done */
        if ((uint16_t)CALL_ORIG(18, 0x5fe6, FAR_CDECL, 2, FP_OFF(planet), FP_SEG(planet)) != 0) {
          STAT_7A = (uint16_t)(STAT_7A + 1);
          (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, DESIGN_NO, 1, 2, 1);
          QUEUE_CHANGED = 1;
          if (g_current_turn < 5) break;
          const uint32_t pop = PL(planet)->surface_amounts_maybe[3];
          const int16_t rate = (int16_t)CALL_ORIG(28, 0x41d4, FAR_CDECL, 1, g_current_player);
          const uint32_t product = (uint32_t)(int32_t)rate * pop;
          if (!((int32_t)product <= 0x8fc || (int16_t)RATING_GAIN <= 0x23 || (int16_t)PLAYER_LEVEL <= 0)) {
            STAT_7A = (uint16_t)(STAT_7A + 1);
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, DESIGN_NO, 1, 2, 1);
            if (!((int32_t)product <= 0xe10 || (int16_t)RATING_GAIN <= 0x32 || (int16_t)PLAYER_LEVEL <= 1))
              (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, DESIGN_NO, 1, 2, 1);
          }
        }
      }
      /* the design-CHOICE_0E rule: another warship when the planet has none under way */
      if (CHOICE_0E != 0xffff && (int16_t)N_D14_15 < 0x3c && design_slot(CHOICE_0E)->build_progress_maybe < 0x1388 && RAND(2) == 0) {
        uint32_t rate = 0;
        for (uint16_t k = 0; (int16_t)k < (int16_t)g_fleet_count; k++) {
          const uint32_t fleet = fleet_ptr(k);
          if (fleet == 0) break;
          if (FL(fleet)->planet_id != PL(planet)->id) continue;
          if ((int16_t)fleet_ships(fleet, CHOICE_0E) <= 0) continue;
          if (FL(fleet)->owner != g_current_player) continue;
          const uint16_t a[] = {FP_OFF(fleet), FP_SEG(fleet)};
          rate = far_call(16, 0x193e, a, 2);
          break; /* the original's loop is left after the first fleet that matches */
        }
        if ((int32_t)rate <= 0x3e8) {
          const uint16_t value = (uint16_t)(3 * (int16_t)planet_byte_sum(planet));
          if (((int32_t)(int16_t)value > (int32_t)rate && (int16_t)value > 0x96) || ((int16_t)N_D14_15 < 0x1e && RAND(10) != 0)) {
            N_D14_15 = (uint16_t)(N_D14_15 + 1);
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, CHOICE_0E, 1, 2, 1);
            QUEUE_CHANGED = 1;
          }
        }
      }
      /* the design-0 rule: another item of design 0 when the planet has a fleet of it */
      if (design_slot(0)->hull == 5 && (int16_t)N_D0 < 0x3c && design_slot(CHOICE_0E)->build_progress_maybe < 0x1d4c && RAND(4) == 0) {
        uint16_t count = 0;
        for (uint16_t k = 0; (int16_t)k < (int16_t)g_fleet_count; k++) {
          const uint32_t fleet = fleet_ptr(k);
          if (fleet == 0) break;
          if (FL(fleet)->planet_id != PL(planet)->id) continue;
          if ((int16_t)FL(fleet)->ship_count[0] <= 0) continue;
          if (FL(fleet)->owner != g_current_player) continue;
          count = FL(fleet)->ship_count[0];
          break; /* the original's loop is left after the first fleet that matches */
        }
        if (!((int16_t)count >= 0x0a && ((int16_t)count >= 0x11 || RAND(10) != 0))) {
          const uint16_t draw = RAND((uint16_t)(count + count + 1));
          if (draw == 0) {
            N_D0 = (uint16_t)(N_D0 + 3);
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, draw, 4, 2, 1);
            QUEUE_CHANGED = 1;
          }
        }
      }
      LOW_THIRD = 0;
      {
        uint16_t first = 0;
        while ((int16_t)first <= 2 && (int32_t)PL(planet)->surface_amounts_maybe[first] >= 0x1388) first++;
        LOW_THIRD = first == 2 ? 1 : 0;
      }
      /* the design-CHOICE_08 rule */
      if (CHOICE_08 != 0xffff && (int16_t)N_D2_9 < 0x8c && !((int16_t)N_D2_9 >= 0x3c && (int16_t)RATING_GAIN <= 0x7d0)) {
        bool order = (int16_t)N_D2_9 > 0x6e && RAND(3) == 0;
        for (uint16_t k = 0; !order && (int16_t)k < (int16_t)g_fleet_count; k++) {
          const uint32_t fleet = fleet_ptr(k);
          if (fleet == 0) break;
          if (FL(fleet)->planet_id != PL(planet)->id) continue;
          if (FL(fleet)->owner != g_current_player) continue;
          if (ai5_fleet_strong_enough(fleet, 0) == 0) continue;
          const uint16_t ships = (uint16_t)(FL(fleet)->ship_count[8] + FL(fleet)->ship_count[9]);
          if ((int16_t)ships < (int16_t)(uint16_t)g_ai_threshold_2) order = true;
          break;
        }
        if (order) {
          N_D2_9 = (uint16_t)(N_D2_9 + 2);
          (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, CHOICE_08, LOW_THIRD == 0 ? 4 : 0x0c, 2, 1);
          QUEUE_CHANGED = 1;
          break; /* the planet is done */
        }
      }
      /* the quota rule: the quota items for a planet with enough of everything */
      if ((int16_t)QUOTA > 0 && (int32_t)PL(planet)->surface_amounts_maybe[3] > 0x2710 && !production_list_has(match_item_3400)) {
        uint16_t first = 0;
        while ((int16_t)first <= 2 && (int32_t)PL(planet)->surface_amounts_maybe[first] >= 0x7d0) first++;
        if (first != 2) {
          const uint16_t sum = planet_byte_sum(planet);
          bool build;
          if ((int16_t)sum < 0x0f) build = true;
          else if ((int16_t)sum < 0x1e && RAND(3) != 0) build = true;
          else build = !((int16_t)sum >= 0x3c) && RAND(5) != 0;
          if (build) {
            QUOTA = (uint16_t)(QUOTA - 1);
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x0d, 1, 1, 1);
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, 0x0c, 0x4b, 1, 1);
            QUEUE_CHANGED = 1;
          }
        }
      }
      /* the design-CHOICE_02 rule */
      if (CHOICE_02 != 0xffff && (int16_t)N_D2_9 < 0x82 && g_current_turn > 0x14 &&
          !((int16_t)N_D2_9 >= 0x32 && (int16_t)RATING_GAIN <= 0x7d0) && !(LOW_THIRD == 0 && RAND(3) != 0)) {
        const uint16_t pick = (CHOICE_05 != 0xffff && RAND(3) == 0) ? CHOICE_05 : CHOICE_02;
        N_D2_9 = (uint16_t)(N_D2_9 + 1);
        (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, pick, LOW_THIRD == 0 ? 2 : 0x0a, 2, 1);
        QUEUE_CHANGED = 1;
        if (LOW_THIRD != 0) break; /* the planet is done */
      }
      /* the build rule: as many items of design CHOICE_0C as the planet can afford (18:0x3558 / 0x3600: its stock and
       * the cost of one item, 7:0x3b60: the design's cost words) */
      if (CHOICE_0C != 0xffff && !((int16_t)(g_current_turn < 0x78 ? 0x50 : 0x3c) <= (int16_t)N_D12_13 || (int16_t)BUDGET_ANSWER >= 0x7d0)) {
        (void)CALL_ORIG(18, 0x3558, FAR_CDECL, 3, FP_OFF(planet), FP_SEG(planet), (uint16_t)(rt_bp - 0x28));
        (void)CALL_ORIG(18, 0x3600, FAR_CDECL, 3, FP_OFF(planet), FP_SEG(planet), (uint16_t)(rt_bp - 0x38));
        bool affordable = true;
        for (uint16_t k = 0; (int16_t)k < 4; k++) {
          L32(-0x28 + 4 * k) = L32(-0x28 + 4 * k) - L32(-0x38 + 4 * k);
          if ((int16_t)L16(-0x26 + 4 * k) < 0) {
            affordable = false;
            break;
          }
        }
        if (affordable) {
          (void)CALL_ORIG(7, 0x3b60, FAR_CDECL, 4, g_current_player, (uint16_t)(DS_DEFAULT_SHIP_DESIGNS + (uint16_t)(CHOICE_0C * sizeof(struct ShipDesign))),
                          MEM_SEL_DGROUP, (uint16_t)(rt_bp - 0x50));
          uint16_t copies = 0;
          while ((int16_t)copies < 0x14) {
            uint16_t k = 0;
            for (; (int16_t)k < 4; k++) {
              L32(-0x28 + 4 * k) = L32(-0x28 + 4 * k) - L16(-0x50 + 2 * k);
              if ((int16_t)L16(-0x26 + 4 * k) < 0) break;
            }
            if ((int16_t)k < 4) break;
            copies++;
          }
          if ((int16_t)copies > 0) {
            QUEUE_CHANGED = 1;
            (void)CALL_ORIG(18, 0x2732, FAR_CDECL, 4, CHOICE_0C, copies, 2, 1);
            N_D12_13 = (uint16_t)(N_D12_13 + 1);
          }
        }
      }
    } while (0);
    if (flush) flush_queue(QUEUE_CHANGED);
  }

  /* ---- 4a. the first walk over the fleets ---- */
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  OWN_CHAIN = 0;
  OTHER_CHAIN = 0;
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_fleet_count; i++) {
    const uint32_t fleet = fleet_ptr(i);
    if (fleet == 0) break;
    if (fleet_has_far_waypoint_kind(fleet, 8)) {
      /* a far waypoint of kind 8: squared distance of the fleet to the waypoint */
      const int16_t dx = (int16_t)(FL(fleet)->x - waypoint_of(fleet, 1)->x);
      const int16_t dy = (int16_t)(FL(fleet)->y - waypoint_of(fleet, 1)->y);
      const uint32_t dist2 = (uint32_t)((int32_t)dx * dx) + (uint32_t)((int32_t)dy * dy);
      if ((int32_t)dist2 > 0x9c40) select_fleet_for_waypoints(fleet);
    }
    if (FL(fleet)->owner != g_current_player) {
      WD(FP_SEG(fleet), FP_OFF(fleet), FLEET_CHAIN_LINK, OTHER_CHAIN);
      OTHER_CHAIN = fleet;
      continue;
    }
    /* the player's own fleet: `to_tail` says that the fleet goes on to the orders at the end of this walk */
    bool to_tail = true;
    const uint16_t here_id = FL(fleet)->planet_id;
    do {
      if ((int16_t)FL(fleet)->ship_count[0] > 0 && g_current_turn > 0x28) {
        to_tail = false;
        if ((int16_t)FL(fleet)->waypoint_count > 1) {
          if ((waypoint_of(fleet, 0)->flags & 0xf) != 0) (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 1);
          break;
        }
        if (((int16_t)N_D0_START > 0x37 || ((int16_t)N_D0_START > 0x28 && RAND(3) != 0)) &&
            (uint16_t)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, FP_OFF(fleet), FP_SEG(fleet), 0, 0, 0x48, 0, 0x6c, 0) != 0)
          break;
        if ((int16_t)FL(fleet)->ship_count[0] >= 7 && RAND(5) == 0) {
          const uint16_t found = (uint16_t)CALL_ORIG(18, 0x3ad8, FAR_CDECL, 4, FL(fleet)->x, FL(fleet)->y, 0x69, 1);
          if (found != 0xffff && found != FL(fleet)->planet_id) {
            (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 1);
            ORDER_13C.target_id = found;
            ORDER_13C.flags = (uint16_t)((ORDER_13C.flags & 0xf1ff) | 0x0100);
            const uint32_t xy = planet_xy_at((uint16_t)(found << 2));
            ORDER_13C.x = (uint16_t)xy;
            ORDER_13C.y = (uint16_t)(xy >> 16);
            ORDER_13C.flags = (uint16_t)((ORDER_13C.flags & 0xfff6) | 6);
            ORDER_13C.flags = (uint16_t)(ORDER_13C.flags | 0x1000);
            ORDER_13C.flags = (uint16_t)((ORDER_13C.flags & 0xff4f) | 0x40);
            (void)CALL_ORIG(18, 0x2664, FAR_CDECL, 4, FP_OFF(fleet), FP_SEG(fleet), (uint16_t)(rt_bp - 0x13c), 0);
            to_tail = true;
            break;
          }
        }
        if ((waypoint_of(fleet, 0)->flags & 0xf) == 6) {
          to_tail = true;
          break;
        }
        select_fleet_with_order(fleet, 0xfff6, 6, true);
        break;
      }
      if (!((int16_t)FL(fleet)->ship_count[14] > 0 || (int16_t)FL(fleet)->ship_count[15] > 0) || FL(fleet)->waypoint_count != 1) {
        if ((uint16_t)CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, FP_OFF(fleet), FP_SEG(fleet)) != 0) {
          WD(FP_SEG(fleet), FP_OFF(fleet), FLEET_CHAIN_LINK, OWN_CHAIN);
          OWN_CHAIN = fleet;
          uint16_t design = 2;
          while (!((int16_t)DESIGN_BOUND < (int16_t)design) && (int16_t)FL(fleet)->ship_count[design] <= 0) design++;
          if ((int16_t)DESIGN_BOUND < (int16_t)design) break;
          if (!fleet_has_far_waypoint_kind(fleet, 1) && here_id == 0xffff) break;
          const uint16_t target_planet = fleet_has_far_waypoint_kind(fleet, 1) ? waypoint_of(fleet, 1)->target_id : here_id;
          uint8_t *byte = planet_info_byte(target_planet, 0xa);
          if (*byte != 0) *byte |= 0x80;
          break;
        }
        if ((uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, FP_OFF(fleet), FP_SEG(fleet)) == 0) break;
        uint16_t planet_id = 0xffff;
        if ((int16_t)FL(fleet)->waypoint_count > 1 && (waypoint_of(fleet, 0)->flags & 0xf) == 0) {
          if (waypoint_kind(fleet, 1) == 1) planet_id = waypoint_of(fleet, 1)->target_id;
        } else {
          planet_id = FL(fleet)->planet_id;
        }
        if (planet_id == 0xffff) break;
        const uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, planet_id);
        if (planet != 0 && PL(planet)->owner == g_current_player) break;
        if (FL(fleet)->cargo[3] != 0) break;
        select_fleet_for_waypoints(fleet);
        (void)CALL_ORIG(18, 0x3bb4, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 0);
        break;
      }
      /* a fleet of designs 14 and 15 with exactly one waypoint */
      if (((int16_t)N_D14_15 > 0x3a || ((int16_t)N_D14_15 > 0x30 && RAND(3) != 0)) &&
          (uint16_t)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, FP_OFF(fleet), FP_SEG(fleet), 0x0e, 0x0f, 0x48, 0, 0x6c, 0) != 0) {
        to_tail = false;
        break;
      }
      if ((waypoint_of(fleet, 0)->flags & 0xf) != 3) {
        select_fleet_with_order(fleet, 0xfff3, 3, false);
        to_tail = false;
        break;
      }
      if (here_id != 0xffff) {
        const uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, here_id);
        if (planet != 0 && PL(planet)->owner == g_current_player) {
          const uint16_t sum = planet_byte_sum(planet);
          if ((int16_t)sum < 0x1e || ((int16_t)sum < 0x3c && RAND(3) == 0)) {
            const uint16_t a[] = {FP_OFF(fleet), FP_SEG(fleet)};
            (void)far_call(20, 0x2286, a, 2);
            to_tail = false;
            break;
          }
        }
      }
      if (RAND(10) == 0) {
        const uint16_t a[] = {FP_OFF(fleet), FP_SEG(fleet)};
        (void)far_call(20, 0x2286, a, 2);
      }
    } while (0);
    /* 4a'. the orders at the end of the walk: early game, or a fleet of design 1 or 7 */
    do {
      if (!to_tail) break;
      if (g_current_turn <= 0x0a && ((int16_t)FL(fleet)->ship_count[0] > 0 || FL(fleet)->ship_count[2] != 0)) {
        select_fleet_with_order(fleet, 0xfff5, 5, false);
        break;
      }
      if ((int16_t)FL(fleet)->waypoint_count > 1) {
        if (SLOT7_CLAIMED == 0 || WANTED(7) == 0 || FL(fleet)->ship_count[7] == 0) break;
        select_fleet_for_waypoints(fleet);
        (void)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 0);
        break;
      }
      if (FL(fleet)->ship_count[1] == 0 && (SLOT7_CLAIMED == 0 || FL(fleet)->ship_count[7] == 0)) break;
      if (SLOT7_CLAIMED != 0 && DESIGN_NO == 1 && FL(fleet)->planet_id != 0xffff) {
        select_fleet_with_order(fleet, 0xfff5, 5, false);
        break;
      }
      if ((int16_t)PLAYER_LEVEL > 1 && FL(fleet)->planet_id != 0xffff) {
        const uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, FL(fleet)->planet_id);
        if (planet != 0 && PL(planet)->owner != 0xffff && PL(planet)->owner != g_current_player && (PL(planet)->flags & 2) == 0 &&
            (uint16_t)(PL(planet)->population_estimate_maybe & 0x0fff) < 0x32)
          break;
        if (planet != 0 && PL(planet)->owner == 0xffff) {
          if (FL(fleet)->cargo[3] == 0) (void)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 1);
          else select_fleet_with_order(fleet, 0xfff2, 2, false);
          break;
        }
      }
      const uint16_t target = (uint16_t)CALL_ORIG(18, 0x098a, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), (uint16_t)(rt_bp - 0x48));
      if (target == 0xffff && L32(-0x48) == 0) {
        if (FL(fleet)->planet_id != 0xffff) select_fleet_with_order(fleet, 0xfff5, 5, false);
        break;
      }
      (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FL(fleet)->id);
      if (FL(fleet)->planet_id != 0xffff) {
        const uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, FL(fleet)->planet_id);
        int32_t amount = 0;
        if (planet != 0) {
          amount = (int32_t)PL(planet)->surface_amounts_maybe[3] / 10;
          if (amount > 0x19) amount = 0x19;
        }
        (void)CALL_ORIG(18, 0x0750, FAR_CDECL, 6, 1, FL(fleet)->planet_id, 2, FL(fleet)->id, 3, (uint16_t)amount);
        (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, FL(fleet)->id, 0x4b8c);
      }
      if (target != 0xffff) (void)CALL_ORIG(18, 0x0864, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), target);
      else (void)CALL_ORIG(18, 0x08fe, FAR_CDECL, 4, FP_OFF(fleet), FP_SEG(fleet), L16(-0x48), L16(-0x46));
    } while (0);
  }

  /* ---- 4b. the second walk: the player's idle fleets get 20:0x23ba ---- */
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_fleet_count; i++) {
    const uint32_t fleet = fleet_ptr(i);
    if (fleet == 0) break;
    if (FL(fleet)->owner != g_current_player || (int16_t)FL(fleet)->waypoint_count > 1) continue;
    if ((uint16_t)CALL_ORIG(18, 0x2e90, FAR_CDECL, 2, FP_OFF(fleet), FP_SEG(fleet)) == 0) continue;
    if (FL(fleet)->battle_plan != 4) {
      (void)CALL_ORIG(9, 0x2a74, FAR_CDECL, 2, 2, FL(fleet)->id);
      g_waypoint_mode_maybe = 4;
      (void)CALL_ORIG(7, 0x153e, FAR_CDECL, 2, 0xffff, 0x4b8c);
    }
    const uint16_t a[] = {FP_OFF(fleet), FP_SEG(fleet)};
    (void)far_call(20, 0x23ba, a, 2);
  }

  /* ---- 4c. the third walk: each of the player's fleets gets its order ---- */
  (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, 0xfc62);
  for (uint16_t i = 0; (int16_t)i < (int16_t)g_fleet_count; i++) {
    const uint32_t fleet = fleet_ptr(i);
    if (fleet == 0) break;
    if (FL(fleet)->owner != g_current_player) continue;
    uint16_t design = 0;
    while ((int16_t)design < 0x10 && !((int16_t)FL(fleet)->ship_count[design] > 0 && WANTED(design) == 0)) design++;
    if (design == 0x10) {
      if (FL(fleet)->planet_id != 0xffff) {
        const uint32_t planet = CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, FL(fleet)->planet_id);
        if (planet != 0 && PL(planet)->owner == g_current_player && ((PL(planet)->flags & 2) != 0 || RAND(5) == 0)) {
          select_fleet_with_order(fleet, 0xfff5, 5, false);
          continue;
        }
      }
      if (FL(fleet)->planet_id == 0xffff && fleet_has_far_waypoint_kind(fleet, 1)) continue;
      if ((uint16_t)CALL_ORIG(18, 0x4510, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 0) != 0) continue;
    }
    /* the first design from 2 to 9 with ships (design 7 is left to the claimed slot) decides how it is ordered */
    uint16_t found = 2;
    while ((int16_t)found <= 9 && ((int16_t)FL(fleet)->ship_count[found] <= 0 || (found == 7 && SLOT7_CLAIMED != 0))) found++;
    if ((int16_t)found <= 9) {
      const uint16_t a[] = {FP_OFF(fleet), FP_SEG(fleet)};
      bool strong_order = (int16_t)N_D2_9 > 0x64 || ((int16_t)N_D2_9 > 0x5a && RAND(3) == 0);
      if (strong_order) {
        int32_t total = 0;
        for (uint16_t d = 2; (int16_t)d <= 9; d++) total += (int16_t)FL(fleet)->ship_count[d];
        const uint16_t draw = RAND(100);
        strong_order = (int32_t)(int16_t)draw > total - 10 || RAND(20) == 0;
      }
      if (!strong_order || (uint16_t)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, FP_OFF(fleet), FP_SEG(fleet), 2, 9, 0x64, 0, 0xc8, 0) == 0)
        (void)far_call(20, 0x2998, a, 2);
      continue;
    }
    if ((uint16_t)CALL_ORIG(18, 0x2db2, FAR_CDECL, 2, FP_OFF(fleet), FP_SEG(fleet)) == 0) continue;
    if (fleet_has_far_waypoint_kind(fleet, 2)) continue;
    const bool ask = (int16_t)(g_current_turn <= 0x78 ? 0x46 : 0x32) < (int16_t)N_D12_13 ||
                     ((int16_t)(g_current_turn <= 0x78 ? 0x3c : 0x28) < (int16_t)N_D12_13 && RAND(3) == 0);
    if (ask && !((int16_t)fleet_ships(fleet, CHOICE_0C) >= 0x14 && RAND(20) != 0) &&
        (uint16_t)CALL_ORIG(18, 0x5e82, FAR_CDECL, 8, FP_OFF(fleet), FP_SEG(fleet), 0x0c, 0x0d, 0x24, 0, 0x48, 0) != 0)
      continue;
    (void)CALL_ORIG(18, 0x1434, FAR_CDECL, 7, FP_OFF(fleet), FP_SEG(fleet), (uint16_t)OWN_CHAIN, (uint16_t)(OWN_CHAIN >> 16),
                    (uint16_t)OTHER_CHAIN, (uint16_t)(OTHER_CHAIN >> 16), (uint16_t)((g_game_flags & 0x10) >> 4));
  }

  /* ---- 5. restore the state ---- */
  (void)CALL_ORIG(18, 0x59ba, FAR_CDECL, 5, SAVED_STATE, turn_arg, 0, (uint16_t)(rt_bp - 0x28), (uint16_t)(rt_bp - 0x38));
  (void)rt_call_far(MK_FP(SEG_SEL(21), 0x1e2a), FAR_CDECL, NULL, 0);
  FRAME_LEAVE();
}
