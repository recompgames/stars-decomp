/* Decompiled code segment 22 (NE segment 22, selector 0x00B0).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg22.c; Ghidra's pseudo-C in re/decomp/seg22 is only a reading aid.
 *
 * Segment 22 is the turn generation: the host runs 22:0x0000 once per turn; the functions around it move the
 * fleets and the map objects, carry out the waypoint orders, refuel and detect minefield hits:
 *   22:0x0000  generate_turn: load the game, read the orders, run the turn phases, write the files
 *   22:0x0e92  run_fleet_order_phases: sync the planet flags, then the fleet order passes of 22:0x3ec6
 *   22:0x0f6e  move_map_objects: mineral packets, wormholes and the mystery trader move
 *   22:0x1cfa  regenerate_fleet_fuel: refuel the fleets at friendly starbases or with fuel generators
 *   22:0x1f18  move_fleets: the fleets move along their courses
 *   22:0x30b6  fleet_hits_minefield: does a fleet's course cross an enemy minefield, and what the mines destroy
 *   22:0x3ec6  execute_waypoint_orders: the order of every fleet's first waypoint (transport, colonize, ...)
 * The segment is 24566 bytes (0x5ff6); 22:0x3ec6 runs to 0x5ff5 (its row says 8496 bytes: the jump tables at
 * 0x4312 and 0x4422 are inside it) and there is a table of 18 words at 0x0e6e that 22:0x30b6 reads through CS.
 *
 * Names: Ghidra's are all generic (FUN_10a8_<off>), no function has an export name, so the C names describe what
 * each function does (CONVENTIONS rule f); they are descriptive, not the game's. The old FUN_10a8_<off> names are kept
 * in the comment above each function.
 *
 * All seven functions are written as readable C (helpers with game names, structs, if/for/switch; no goto, no register
 * emulation). They keep the original's reads, writes, widths, order of calls and messages; where the original reads a stack slot
 * before it writes it (a value left by an earlier fleet or call) or hands a callee the address of a slot, the slot stays a BP_LOCAL
 * at the original offset (WP_COPY, PLANET_HERE, LOCATION_A, LAST_REMOVED ...). Quirks of the original are kept and commented, for
 * example that 22:0x3ec6 keeps the mine-laying message id of an earlier mine type. The DIFF tests prove the equivalence.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with n the ENTER size and
 * one push for each PUSH SI / PUSH DI. Callees that are decompiled are reached through rt_call_far (far_call), the others with
 * CALL_ORIG; Win16 APIs by name (MulDiv, SetCursor, LoadCursor, _wsprintf) and the C runtime's sqrt and ftol (34:0x0d68,
 * 34:0x0de6) through the stand-ins of src/crt34.h. 22:0x30b6 and 22:0x0f6e still use the x87 stack helpers below. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/fp80.h"
#include "src/crt34.h"
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
__attribute__((unused)) static inline void wr32(uint16_t seg, uint16_t off, uint32_t v) { memcpy(LIN(MK_FP(seg, off)), &v, 4); }
/* DGROUP (DS) bytes, words and dwords. */
static inline uint8_t drd8(uint16_t off) { return rd8(MEM_SEL_DGROUP, off); }
__attribute__((unused)) static inline void dwr8(uint16_t off, uint8_t v) { wr8(MEM_SEL_DGROUP, off, v); }
__attribute__((unused)) static inline uint16_t drd16(uint16_t off) { return rd16(MEM_SEL_DGROUP, off); }
static inline void dwr16(uint16_t off, uint16_t v) { wr16(MEM_SEL_DGROUP, off, v); }
static inline uint32_t drd32(uint16_t off) { return rd32(MEM_SEL_DGROUP, off); }

/* Records of src/stars.h at a far address. */
#define FLEET(seg, off) FAR_REC(struct Fleet, (seg), (off))
#define PLANET(seg, off) FAR_REC(struct Planet, (seg), (off))
/* The offset of a field of a record of src/stars.h (checked against the original's offset by STATIC_OFFSET there). */
#define FLD(type, f) ((uint16_t)offsetof(type, f))
/* A far pointer global (offset word first) as its two words. */
#define FAR_WORDS(g) (*(struct FarPtrWords *)&(g))
/* Stack locals of the current frame, at the original BP offsets. */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))

/* Call a far CDECL function of the game with word arguments (rt_call_far: a callee that mirrors a
 * frame is never called by name, rule e). */
static uint32_t far_call(uint16_t seg, uint16_t off, const uint16_t *args, int16_t n)
{
  return rt_call_far(MK_FP(SEG_SEL(seg), off), FAR_CDECL, args, n);
}

/* The far pointer to entry `at / 4` of the fleet pointer array (DS:0xBA): the offset word first. */
static uint32_t fleet_ptr_at(uint16_t at)
{
  return rd32(FAR_WORDS(g_fleet_table).seg, (uint16_t)(FAR_WORDS(g_fleet_table).off + at));
}

/* The relation byte DS:0x5ad4 + 0xc0 * row + other: byte `other` of Player.relations of the player `row`
 * (the index is not range checked in the original). */
static inline uint8_t player_relation(uint16_t row, uint16_t other)
{
  return drd8((uint16_t)(DS_PLAYERS + FLD(struct Player, relations) + (uint16_t)(row * sizeof(struct Player)) + other));
}

/* ---- stack slots and arithmetic ---- */
#define L8(o) BP_LOCAL(uint8_t, (o))
#define L16(o) BP_LOCAL(uint16_t, (o))
#define L32(o) BP_LOCAL(uint32_t, (o))
/* DIV / IDIV: a divide error (zero divisor or a quotient that does not fit) stops the run (rule b); `where` numbers the check. */
static void divide_error(uint32_t where)
{
  fprintf(stderr, "22: divide error (check %u)\n", where);
  abort();
}
/* x87 stack of the translated functions: fst[fn - 1] is ST(0). */
#define ST(i) fst[fn - 1 - (i)]
#define FPUSH(v) (fst[fn++] = (v))
static inline double rdf64(uint16_t seg, uint16_t off) {
  double d;
  memcpy(&d, LIN(MK_FP(seg, off)), 8);
  return d;
}
__attribute__((unused)) static inline void wrf64(uint16_t seg, uint16_t off, double d) { memcpy(LIN(MK_FP(seg, off)), &d, 8); }
__attribute__((unused)) static inline float rdf32(uint16_t seg, uint16_t off) {
  float f;
  memcpy(&f, LIN(MK_FP(seg, off)), 4);
  return f;
}
/* FCOM: the condition bits C0 (less), C2 (unordered) and C3 (equal) of the x87 status word. */
static void fp_cmp(Fp80 a, Fp80 b, uint32_t *c0, uint32_t *c2, uint32_t *c3)
{
  if (extF80_eq(a, b)) {
    *c0 = 0, *c2 = 0, *c3 = 1;
  } else if (extF80_lt(a, b)) {
    *c0 = 1, *c2 = 0, *c3 = 0;
  } else if (extF80_lt(b, a)) {
    *c0 = 0, *c2 = 0, *c3 = 0;
  } else {
    *c0 = 1, *c2 = 1, *c3 = 1;
  }
}

/* ---- callees and records used by several functions of this segment ---- */
/* A random number below n from the game's generator, 8:0x8730(n). */
static uint16_t rand_below(uint16_t n) { return (uint16_t)CALL_ORIG(8, 0x8730, FAR_CDECL, 1, n); }
/* The race value (Player.attrs[14]) of a player: 28:0x21ee(player row, 14). */
static uint16_t player_race(uint16_t player)
{
  return (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player))), PLAYER_ATTR_RACE);
}
/* A message for a player: 6:0x7344(player, id, subject, seven values). */
static void send_message(uint16_t player, uint16_t id, uint16_t subject, uint16_t v0, uint16_t v1, uint16_t v2, uint16_t v3,
                         uint16_t v4, uint16_t v5, uint16_t v6)
{
  const uint16_t a[10] = {player, id, subject, v0, v1, v2, v3, v4, v5, v6};
  (void)far_call(6, 0x7344, a, 10);
}
/* A message with two values: 6:0x7328(player, id, subject, v0, v1). */
static void send_message2(uint16_t player, uint16_t id, uint16_t subject, uint16_t v0, uint16_t v1)
{
  const uint16_t a[5] = {player, id, subject, v0, v1};
  (void)far_call(6, 0x7328, a, 5);
}
/* The end of the map object array (a 16-bit offset, as the original computes it from DS:0x188e and the count DS:0x1892). */
static inline uint16_t map_objects_end(void)
{
  return (uint16_t)(g_map_objects + (uint16_t)((int16_t)g_map_object_count * (int16_t)sizeof(struct MapObject)));
}
/* The far pointer to the ship designs of a player (DS:0xBE, 16 designs of 0x8D bytes), and design `slot` of it. */
static inline uint32_t player_designs(uint16_t player) { return drd32((uint16_t)(DS_SHIP_DESIGNS + (uint16_t)(player << 2))); }
static inline struct ShipDesign *design_of(uint32_t designs, uint16_t slot)
{
  return FAR_REC(struct ShipDesign, FP_SEG(designs), (uint16_t)(FP_OFF(designs) + (uint16_t)(slot * sizeof(struct ShipDesign))));
}
/* A design's mask of the players who have seen it (ShipDesign.fld_8b, zeroed when the game is loaded). */
static inline void design_seen_by(struct ShipDesign *design, uint16_t players) { design->fld_8b |= players; }
/* Byte +7 of a map object (the high byte of MapObject.fld_06; for a packet bits 2 to 5 are its warp). */
static inline uint8_t map_object_byte7(uint16_t seg, uint16_t off) { return (uint8_t)(FAR_REC(struct MapObject, seg, off)->fld_06 >> 8); }
/* A double constant of the data segment. */
static inline double ds_double(uint16_t off) { return rdf64(MEM_SEL_DGROUP, off); }

/* regenerate_fleet_fuel: 22:0x1cfa (Ghidra FUN_10a8_1cfa), 542 bytes. ENTER 0x30,0; PUSH DI; PUSH SI.
 * FAR CDECL, no arguments, no result.
 * Walks the fleet pointer array (up to the first null entry) and refuels every fleet that is not empty
 * (flags bit 2 clear):
 *  - a fleet in orbit of a planet that has a starbase (planet flags bit 1) of its own or of a player whose
 *    relation byte for the fleet's owner is 1 is refuelled to its maximum (10:0x292a(fleet, 1)) when
 *    the starbase design's hull record (2:0x507c) has a non-zero word at +0x34;
 *  - every other fleet gains fuel from its fuel generators: for each of its 16 ship designs with ships, 50
 *    times (ships * count) for every slot of category 0x800 and item 0x10 (provisionally the antimatter
 *    generator, "_maybe"), plus 200 per ship of hull 0x19 or 0x1a (the fuel transports). The fuel is
 *    capped at the maximum, 10:0x292a(fleet, 1). */
DECOMP(22, 0x1cfa, regenerate_fleet_fuel, FAR_CDECL)
void regenerate_fleet_fuel(void)
{
  FRAME_ENTER_REGS(0x30, 2);
  uint16_t at = 0;
  for (int16_t i = 0; (int16_t)g_fleet_count > i; i++, at = (uint16_t)(at + sizeof(FARPTR))) {
    const uint32_t fleet = fleet_ptr_at(at);
    const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
    bool full_refuel = false;
    if (fleet == 0) break;
    if ((FLEET(fseg, foff)->flags & 4) != 0) continue;
    if (FLEET(fseg, foff)->planet_id != 0xffff) {
      const uint16_t pseg = g_planet_table_segment_maybe;
      const uint16_t poff = (uint16_t)((uint16_t)(FLEET(fseg, foff)->planet_id * sizeof(struct Planet)) + g_planet_table);
      if ((PLANET(pseg, poff)->flags & 2) != 0 && PLANET(pseg, poff)->owner != 0xffff &&
          (PLANET(pseg, poff)->owner == FLEET(fseg, foff)->owner ||
           player_relation(PLANET(pseg, poff)->owner, FLEET(fseg, foff)->owner) == 1)) {
        /* the starbase design: design number = low nibble of the planet's starbase dword */
        const uint16_t design = (uint16_t)((uint8_t)(rd8(pseg, (uint16_t)(poff + FLD(struct Planet, starbase))) & 0xf) * 0x8d);
        const uint32_t sb_designs = drd32((uint16_t)(DS_SB_DESIGNS + (uint16_t)(PLANET(pseg, poff)->owner << 2)));
        const uint16_t hull = rd16(FP_SEG(sb_designs), (uint16_t)(FP_OFF(sb_designs) + design));
        const uint16_t a_hull[1] = {hull};
        const uint32_t rec = far_call(2, 0x507c, a_hull, 1);
        full_refuel = rd16(FP_SEG(rec), (uint16_t)(FP_OFF(rec) + 0x34)) != 0;
      }
    }
    if (!full_refuel) {
      /* fuel generated by the fleet's ships */
      uint32_t slot_gen = 0, hull_gen = 0;
      uint32_t design = drd32((uint16_t)(DS_SHIP_DESIGNS + (uint16_t)(FLEET(fseg, foff)->owner << 2)));
      uint16_t count_at = (uint16_t)(foff + FLD(struct Fleet, ship_count));
      for (int16_t k = 0x10; k != 0; k--) {
        const int16_t ships = (int16_t)rd16(fseg, count_at);
        if (ships != 0) {
          const uint16_t dseg = FP_SEG(design);
          const uint16_t doff = FP_OFF(design);
          for (int16_t s = (int16_t)(uint8_t)rd8(dseg, (uint16_t)(doff + FLD(struct ShipDesign, slot_count))) - 1; s >= 0; s--) {
            const uint16_t slot = (uint16_t)(doff + FLD(struct ShipDesign, slots) + (uint16_t)(s * sizeof(struct DesignSlot)));
            if (rd16(dseg, slot) == 0x800) {
              const uint16_t item = rd16(dseg, (uint16_t)(slot + 2));
              if ((uint8_t)item == 0x10) slot_gen = slot_gen + (uint32_t)(int32_t)ships * (uint32_t)(item >> 8);
            }
          }
          if (rd16(dseg, doff) == 0x19 || rd16(dseg, doff) == 0x1a)
            hull_gen = hull_gen + (uint32_t)(int32_t)((int32_t)ships * 200);
        }
        design = MK_FP(FP_SEG(design), (uint16_t)(FP_OFF(design) + sizeof(struct ShipDesign)));
        count_at = (uint16_t)(count_at + 2);
      }
      if (hull_gen != 0 || slot_gen != 0) {
        const uint32_t gain = (uint32_t)(slot_gen * 0x32);
        const uint16_t a_fuel[3] = {foff, fseg, 1};
        const uint32_t max_fuel = far_call(10, 0x292a, a_fuel, 3);
        const uint32_t sum = (uint32_t)(FLEET(fseg, foff)->fuel + gain + hull_gen);
        if ((int32_t)max_fuel < (int32_t)sum) full_refuel = true;
        else FLEET(fseg, foff)->fuel = (uint32_t)(FLEET(fseg, foff)->fuel + (uint32_t)(gain + hull_gen));
      }
    }
    if (full_refuel) {
      const uint16_t a_fuel[3] = {foff, fseg, 1};
      const uint32_t max_fuel = far_call(10, 0x292a, a_fuel, 3);
      wr16(fseg, (uint16_t)(foff + FLD(struct Fleet, fuel)), (uint16_t)max_fuel);
      wr16(fseg, (uint16_t)(foff + FLD(struct Fleet, fuel) + 2), (uint16_t)(max_fuel >> 16));
    }
  }
  FRAME_LEAVE();
}

/* run_fleet_order_phases: 22:0x0e92 (Ghidra FUN_10a8_0e92), 219 bytes. ENTER 8,0; PUSH DI. FAR CDECL
 * (flag [BP+6], a word), no result.
 * Makes the turn's fleet phases run: first sets bit 5 of every planet's flags byte to "has an owner"
 * (owner word not 0xffff), a word xor on the word at +4. With the flag set it stores (turn & 0xf) << 8 | 1
 * in DS:0xdcc and calls 30:0x24aa(flag); then 33:0x0784(flag); with the flag set it also clears the
 * word at +0x32 of every planet. Then runs the fleet pass 22:0x3ec6 with 1 (flag 0) or 3 (flag set), calls
 * 23:0x1e82 and 23:0x4c50(0), runs 22:0x3ec6 with 2 (flag 0) or 4 (flag set) and, with the flag clear, calls
 * 23:0x1c2c. */
DECOMP(22, 0x0e92, run_fleet_order_phases, FAR_CDECL)
void run_fleet_order_phases(uint16_t flag)
{
  FRAME_ENTER_DI(8);
  uint16_t pseg = g_planet_table_segment_maybe;
  uint16_t end = (uint16_t)(g_planet_table + (uint16_t)(g_planet_count * sizeof(struct Planet)));
  for (uint16_t p = g_planet_table; p < end; p = (uint16_t)(p + sizeof(struct Planet))) {
    const uint16_t owned = PLANET(pseg, p)->owner != 0xffff ? 1 : 0;
    /* bit 5 of the flags byte (bit 13 of the word at +4) is made equal to "has an owner" */
    const uint16_t x = (uint16_t)((uint16_t)(((owned << 5) ^ PLANET(pseg, p)->flags) & 0x20) << 8);
    wr16(pseg, (uint16_t)(p + FLD(struct Planet, info_level)), (uint16_t)(rd16(pseg, (uint16_t)(p + FLD(struct Planet, info_level))) ^ x));
  }
  if (flag != 0) {
    /* DS:0xdcc: low byte 1, high byte = the current turn's low nibble */
    dwr16(0x0dcc, (uint16_t)(((uint16_t)(g_current_turn & 0xf) << 8) | 1)); /* DS:0x0dcc has no name in src/globals.h */
    (void)CALL_ORIG(30, 0x24aa, FAR_CDECL, 1, flag);
  }
  {
    const uint16_t a[1] = {flag};
    (void)far_call(33, 0x0784, a, 1);
  }
  if (flag != 0) {
    pseg = g_planet_table_segment_maybe;
    end = (uint16_t)(g_planet_table + (uint16_t)(g_planet_count * sizeof(struct Planet)));
    for (uint16_t p = g_planet_table; p < end; p = (uint16_t)(p + sizeof(struct Planet))) PLANET(pseg, p)->turn = 0;
  }
  {
    const uint16_t a[1] = {(uint16_t)(flag == 0 ? 1 : 3)};
    (void)far_call(22, 0x3ec6, a, 1);
  }
  (void)CALL_ORIG(23, 0x1e82, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x4c50, FAR_CDECL, 1, 0);
  {
    const uint16_t a[1] = {(uint16_t)(flag == 0 ? 2 : 4)};
    (void)far_call(22, 0x3ec6, a, 1);
  }
  if (flag == 0) (void)CALL_ORIG(23, 0x1c2c, FAR_CDECL, 0);
  FRAME_LEAVE();
}
/* ---- 22:0x0f6e move_map_objects: what happens to a map object in one phase ---- */

/* The kind bits of the first word of a map object (MapObject.id_kind & 0xe000): the packet is evidenced by stars.h (kind
 * 0x20 in the owner_kind_bits byte); the wormhole and the mystery trader are named after what 22:0x0f6e does with them. */
enum {
  OBJECT_KIND_WORD_MASK = 0xe000,
  OBJECT_KIND_WORD_PACKET = 0x2000,
  OBJECT_KIND_WORD_WORMHOLE_maybe = 0x4000,
  OBJECT_KIND_WORD_TRADER_maybe = 0x6000
};
/* What the walk over the map objects does with the object it just handled. */
enum ObjectOutcome {
  OBJECT_KEEPS,  /* stays, the walk goes on with the next one */
  OBJECT_DELETE, /* is deleted (33:0x015a) and the walk goes on with the one that took its place */
  OBJECT_GONE    /* a callee already deleted it: the walk goes on with the one that took its place */
};

/* Fields of a moving map object that struct MapObject does not name (they are the target of a trader at +6/+8, the mineral
 * amounts of a packet at +8, +0xa, +0xc, the counter nibble of a trader at +0xa, the byte at +7 of a packet). */
static inline uint32_t object_target_xy(uint16_t seg, uint16_t off) { return rd32(seg, (uint16_t)(off + 6)); }
static inline void set_object_target(uint16_t seg, uint16_t off, uint16_t x, uint16_t y)
{
  wr16(seg, (uint16_t)(off + 6), x);
  wr16(seg, (uint16_t)(off + 8), y);
}
static inline uint16_t object_word_at(uint16_t seg, uint16_t off, uint16_t d) { return rd16(seg, (uint16_t)(off + d)); }
static inline void set_object_word_at(uint16_t seg, uint16_t off, uint16_t d, uint16_t v) { wr16(seg, (uint16_t)(off + d), v); }
/* The low nibble of the byte at +0xa of a trader (how many times it has been announced) and of a packet (its warp). */
static inline uint16_t object_counter_nibble(uint16_t seg, uint16_t off) { return (uint16_t)(rd8(seg, (uint16_t)(off + 0xa)) & 0xf); }
/* Planet record fields that struct Planet does not name: the word at +0x18 (low 12 bits: a count of installations that
 * the packet destroys), +0x2c (starbase design in the low nibble), the colonists dword at +0x28 (surface_amounts_maybe[3]). */
static inline uint16_t planet_installations_maybe(uint16_t seg, uint16_t off) { return (uint16_t)(rd16(seg, (uint16_t)(off + 0x18)) & 0xfff); }

/* Wormhole (phase 1): its level (33:0x0742, 0 to 6) against a roll of 100 decides whether it jumps; otherwise its age counter (bits 2 to
 * 11 of the word at +6) grows. A jump tries up to 100 random places (a place of the universe for a jump, a place within 12 of the
 * old one otherwise); 33:0x0456 grades each place (0 = fine); the first fine place is kept, else the best one.
 * LAST_CONFLICT is the grade of the last place tried (a stack slot of the original, kept across objects). */
/* Stack slots of move_map_objects that are read before they are written (the first read sees what the stack held). */
#define LAST_CONFLICT L16(-0x56)    /* the grade of the last wormhole place tried, kept across objects */
#define SCRATCH_MESSAGE L16(-0x3a)  /* the id of the last trader announcement or packet message, kept across objects */
static void wormhole_step(uint16_t seg, uint16_t off)
{
  const uint32_t old_xy = FAR_REC(struct MapObject, seg, off)->xy;
  const uint16_t args[2] = {off, seg};
  const int16_t level = (int16_t)far_call(33, 0x0742, args, 2);
  const bool jumps = level > (int16_t)rand_below(100);
  uint16_t universe = 0;
  if (jumps) {
    set_object_word_at(seg, off, 8, 0);
    universe = (uint16_t)((int16_t)(g_universe_size_index + 1) * 0x190);
    set_object_word_at(seg, off, 6, (uint16_t)(object_word_at(seg, off, 6) & 0xf003));
  } else {
    const uint16_t w = object_word_at(seg, off, 6);
    set_object_word_at(seg, off, 6, (uint16_t)(w ^ (uint16_t)(((uint16_t)(w + 4) ^ w) & 0xffc)));
  }
  int16_t best = 0x10;
  uint32_t best_xy = 0;
  uint16_t conflict = LAST_CONFLICT;
  for (uint16_t tries = 0; (int16_t)tries < 100; tries++) {
    if (jumps) {
      wr16(seg, (uint16_t)(off + 2), (uint16_t)(rand_below(universe) + 1000));
      wr16(seg, (uint16_t)(off + 4), (uint16_t)(rand_below(universe) + 1000));
    } else {
      wr16(seg, (uint16_t)(off + 2), (uint16_t)(rand_below(25) + (uint16_t)old_xy - 12));
      wr16(seg, (uint16_t)(off + 4), (uint16_t)(rand_below(25) + (uint16_t)(old_xy >> 16) - 12));
    }
    if (FAR_REC(struct MapObject, seg, off)->xy == old_xy) continue;
    conflict = (uint16_t)far_call(33, 0x0456, args, 2);
    if (conflict == 0) break;
    if (best > (int16_t)conflict) {
      best = (int16_t)conflict;
      best_xy = FAR_REC(struct MapObject, seg, off)->xy;
    }
  }
  LAST_CONFLICT = conflict;
  if (conflict != 0) FAR_REC(struct MapObject, seg, off)->xy = best_xy;
}


/* One step of a packet or a trader towards `target`: `speed` units along the straight line (7:0x28ea gives the distance as a
 * double), each coordinate rounded away from zero. True when the object arrives: its distance is at most `speed`, or the step
 * ends exactly on the target. *distance is the distance before the step, in whole units. A course of no length does not move. */
static bool object_step_toward(uint16_t seg, uint16_t off, uint32_t target, int16_t speed, int16_t *distance)
{
  const uint32_t from = FAR_REC(struct MapObject, seg, off)->xy;
  const uint16_t from_x = (uint16_t)from, from_y = (uint16_t)(from >> 16);
  const uint16_t target_x = (uint16_t)target, target_y = (uint16_t)(target >> 16);
  const uint16_t dargs[4] = {from_x, from_y, target_x, target_y};
  const uint16_t dist_at = (uint16_t)far_call(7, 0x28ea, dargs, 4);
  const double dist = ds_double(dist_at);
  const int16_t whole = (int16_t)(uint16_t)fp80_to_i32_ftol(fp80_from_f64(dist));
  *distance = whole;
  if (whole <= speed) return true;
  const Fp80 bias_x = fp80_from_f64(ds_double((int16_t)target_x <= (int16_t)from_x ? 0x1fa0 : 0x1f98));
  const Fp80 bias_y = fp80_from_f64(ds_double((int16_t)target_y <= (int16_t)from_y ? 0x1fa0 : 0x1f98));
  if (!(ds_double(0x1fa8) < dist) && ds_double(0x1fb0) <= dist) return false; /* the course has no length */
  const Fp80 ratio = fp80_div(fp80_from_i16(speed), fp80_from_f64(dist));
  const uint16_t new_x = (uint16_t)(from_x + (uint16_t)fp80_to_i32_ftol(fp80_add(fp80_imul_i16(ratio, (int16_t)(target_x - from_x)), bias_x)));
  const uint16_t new_y =
      (uint16_t)(from_y + (uint16_t)fp80_to_i32_ftol(fp80_add(fp80_mul(fp80_from_i16((int16_t)(target_y - from_y)), ratio), bias_y)));
  if (target == (((uint32_t)new_y << 16) | new_x)) return true;
  wr16(seg, (uint16_t)(off + 2), new_x);
  wr16(seg, (uint16_t)(off + 4), new_y);
  return false;
}

/* The mystery trader picks a new target on the border of the universe: one coordinate on the low or the high edge, the other
 * random along it. */
static void trader_pick_target(uint16_t seg, uint16_t off)
{
  const uint16_t size = g_universe_size_index;
  const uint16_t edge = rand_below(2) != 0 ? 0x3fc : (uint16_t)((int16_t)size * 0x190 + 0x564);
  const uint16_t along = (uint16_t)(rand_below((uint16_t)((int16_t)size * 0x190 + 0x169)) + 0x3fc);
  if (rand_below(2) == 0) set_object_target(seg, off, edge, along);
  else set_object_target(seg, off, along, edge);
}

/* Is there a mystery trader other than the one at `off`? */
static bool other_trader_exists(uint16_t seg, uint16_t off)
{
  const uint16_t end = map_objects_end();
  for (uint16_t cur = g_map_objects; cur < end; cur = (uint16_t)(cur + sizeof(struct MapObject)))
    if (map_object_kind(FAR_REC(struct MapObject, seg, cur)->owner_kind_bits) == 0x60 && cur != off) return true;
  return false;
}

/* Mystery trader (phase 0). Now and then (1 in 25, while its counter nibble is below 13) it is announced to every player, in a third of
 * those cases at a new target on the border of the universe; it then moves towards its target by counter * counter units. One that
 * arrives disappears when another trader exists or by a coin flip, else it jumps to its target, slows down (counter - 2, at
 * least 6), gets a new target and is announced (message 0xc0: the move is skipped that phase).
 * SCRATCH_MESSAGE is the message id of the last announcement (a stack slot of the original, kept across objects). */
static enum ObjectOutcome trader_step(uint16_t seg, uint16_t off, bool *moved_any)
{
  int16_t steps = (int16_t)object_counter_nibble(seg, off);
  bool new_target = false, announce = false;
  if (steps < 13 && rand_below(25) == 0) {
    SCRATCH_MESSAGE = 0x130;
    announce = true;
    new_target = rand_below(3) == 0;
  }
  for (;;) {
    if (new_target) trader_pick_target(seg, off);
    if (announce) {
      set_object_word_at(seg, off, 0xa,
                         (uint16_t)(object_word_at(seg, off, 0xa) ^ (uint16_t)(((uint8_t)object_word_at(seg, off, 0xa) ^ (uint16_t)(steps + 1)) & 0xf)));
      for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++)
        send_message2(player, SCRATCH_MESSAGE, 0xfffa, FAR_REC(struct MapObject, seg, off)->id_kind, 0);
    }
    const uint16_t counter = object_counter_nibble(seg, off);
    const int16_t speed = (int16_t)(counter * counter);
    const uint32_t target = object_target_xy(seg, off);
    *moved_any = true;
    if (SCRATCH_MESSAGE == 0xc0) return OBJECT_KEEPS;
    int16_t distance;
    if (!object_step_toward(seg, off, target, speed, &distance)) return OBJECT_KEEPS;
    /* it has arrived */
    if (other_trader_exists(seg, off) || rand_below(2) == 0) return OBJECT_DELETE;
    FAR_REC(struct MapObject, seg, off)->xy = object_target_xy(seg, off);
    steps = (int16_t)(object_counter_nibble(seg, off) - 2);
    if (steps < 6) steps = 6;
    SCRATCH_MESSAGE = 0xc0;
    new_target = true;
    announce = true;
  }
}

/* Stack slots of 22:0x0f6e whose address a callee gets (the others are C locals). */
#define CATCHER_TIE L16(-0x1a)               /* 9:0x4f76 out: two mass drivers of the best warp */
#define PLANET_HABITABILITY_F32 BP_LOCAL(float, -0x28) /* 7:0x0262 out */
#define SHIFT_C(k) L16(-0x54 + 2 * (k))       /* 9:0x5220 out (three words) */
#define PACKET_LEAK(k) L16(-0x4e + 2 * (k))   /* the minerals of the packet that were not caught, per mineral */
#define SHIFT_A(k) L16(-0x48 + 2 * (k))       /* 9:0x5220 out (three words) */
#define SHIFT_B(k) L16(-0x42 + 2 * (k))       /* 9:0x5220 out (three words) */

/* Player bytes that 22:0x0f6e reads for a race 6 packet (struct Player lists them as padding): Player + 0x10 + axis (a value of the
 * environment axis, here called the ideal) and Player + 0x13 + axis (negative: the shift goes the other way). */
static inline int8_t player_axis_ideal_maybe(uint16_t player, uint16_t axis)
{
  return (int8_t)drd8((uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)) + 0x10 + axis));
}
static inline int8_t player_axis_flag_maybe(uint16_t player, uint16_t axis)
{
  return (int8_t)drd8((uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)) + 0x13 + axis));
}
/* The planet's three environment values: the original ones at +0xf and the current ones at +0xc (Planet.env_original / env_current). */
static inline int8_t planet_env_original(uint16_t seg, uint16_t off, uint16_t axis) { return (int8_t)PLANET(seg, off)->env_original[axis]; }
static inline int8_t planet_env_current(uint16_t seg, uint16_t off, uint16_t axis) { return (int8_t)PLANET(seg, off)->env_current[axis]; }

/* The shift of an environment value that a race 6 packet makes, `step` units towards ... (see the callers). */
static int16_t shift_towards_limit(int8_t value, int16_t step)
{
  /* the original value is below 50: it can go down by value - 1 at most; else up by 99 - value at most */
  if (value < 0x32) {
    int16_t v = (int16_t)(value - 1);
    if (v > step) v = step;
    return (int16_t)-v;
  }
  const int16_t v = (int16_t)-(value - 0x63);
  return v <= step ? v : step;
}

/* The environment shifts of a race 6 packet on one axis: first the original value (by the chance gains), then the current value (by the
 * 100-mineral chunks); messages 0x131 / 0x132 and 0x133 / 0x134 go to the packet's owner (the original sends both to the same player). */
static void packet_shifts_environment(uint16_t planet_seg, uint16_t planet_off, uint16_t pkt_owner, uint16_t axis, int16_t chunks,
                                      int16_t gains, uint16_t axis_tag)
{
  const uint16_t pid = PLANET(planet_seg, planet_off)->id;
  const uint16_t planet_owner = PLANET(planet_seg, planet_off)->owner;
  if (gains > 0) {
    int16_t gain = gains;
    const int8_t original = planet_env_original(planet_seg, planet_off, axis);
    if (player_axis_flag_maybe(pkt_owner, axis) < 0) {
      gain = shift_towards_limit(original, gain);
    } else {
      const int8_t ideal = player_axis_ideal_maybe(pkt_owner, axis);
      if (original < ideal) {
        if (original + gain > ideal) gain = (int16_t)(ideal - original);
      } else if (original <= ideal) {
        gain = 0;
      } else if (original - gain < ideal) {
        gain = (int16_t)(ideal - original);
      } else {
        gain = (int16_t)-gain;
      }
    }
    if (gain != 0) {
      const uint16_t magnitude = (uint16_t)(gain < 0 ? -gain : gain), grows = gain > 0 ? 1 : 0;
      send_message(pkt_owner, 0x131, pid, grows, axis, pid, magnitude, 0, 0, 0);
      if (planet_owner != 0xffff && planet_owner != pkt_owner) send_message(pkt_owner, 0x132, pid, grows, axis, pid, magnitude, 0, 0, 0);
      PLANET(planet_seg, planet_off)->env_original[axis] = (uint8_t)(PLANET(planet_seg, planet_off)->env_original[axis] + (uint8_t)gain);
    }
  }
  if (chunks <= 0) return;
  g_current_player = pkt_owner;
  const uint16_t args[6] = {planet_off, planet_seg, (uint16_t)(rt_bp - 0x48), (uint16_t)(rt_bp - 0x42), (uint16_t)(rt_bp - 0x54), 1};
  const bool ok = (uint16_t)far_call(9, 0x5220, args, 6) != 0;
  g_current_player = 0xffff;
  if (!ok) return;
  const int8_t current = planet_env_current(planet_seg, planet_off, axis);
  if (player_axis_flag_maybe(pkt_owner, axis) < 0) {
    chunks = (int16_t)(chunks / 2);
    if (planet_env_original(planet_seg, planet_off, axis) < 0x32) {
      int16_t v = (int16_t)(current - 1);
      if (v > chunks) v = chunks;
      chunks = (int16_t)-v;
    } else {
      const int16_t v = (int16_t)-(current - 0x63);
      if (v <= chunks) chunks = v;
    }
  } else if ((int16_t)SHIFT_A(axis) != -1) {
    const int16_t low = (int16_t)SHIFT_A(axis);
    if ((int16_t)(current - low) >= chunks) chunks = (int16_t)-chunks;
    else chunks = (int16_t)-(current - low);
  } else if ((int16_t)SHIFT_B(axis) != -1) {
    const int16_t high = (int16_t)SHIFT_B(axis);
    if ((int16_t)(high - current) < chunks) chunks = (int16_t)(high - current);
  } else {
    chunks = 0;
  }
  if (chunks == 0) return;
  PLANET(planet_seg, planet_off)->env_current[axis] = (uint8_t)(PLANET(planet_seg, planet_off)->env_current[axis] + (uint8_t)chunks);
  const uint16_t value = (uint16_t)((uint16_t)(int16_t)planet_env_current(planet_seg, planet_off, axis) | axis_tag);
  const uint16_t grows = chunks > 0 ? 1 : 0;
  send_message(pkt_owner, 0x133, pid, grows, axis, pid, value, 0, 0, 0);
  if (planet_owner != 0xffff && planet_owner != pkt_owner) send_message(pkt_owner, 0x134, pid, grows, axis, pid, value, 0, 0, 0);
}

/* The part of a packet that lands: every mineral amount of the packet goes to the planet in proportion to `landing`; what the mass
 * drivers do not catch kills colonists and installations, in proportion to the planet's habitability (messages 0xd5 to 0xda). */
static enum ObjectOutcome packet_hits_planet(uint16_t seg, uint16_t off)
{
  const uint16_t planet_off = (uint16_t)(g_planet_table + (uint16_t)((int16_t)(object_word_at(seg, off, 6) & 0x3ff) * (int16_t)sizeof(struct Planet)));
  const uint16_t planet_seg = g_planet_table_segment_maybe;
  const uint16_t packet_warp = (uint16_t)(((object_word_at(seg, off, 6) & 0x3c00) >> 10) + 4);
  const uint16_t pkt_owner = map_object_owner(FAR_REC(struct MapObject, seg, off)->owner_kind_bits);
  const uint16_t packet_id = FAR_REC(struct MapObject, seg, off)->id_kind;
  int16_t catcher_warp; /* the warp of the planet's mass drivers (9:0x4f76), one more when two share the best */
  {
    const uint16_t a[3] = {planet_off, planet_seg, (uint16_t)(rt_bp - 0x1a)};
    catcher_warp = (int16_t)far_call(9, 0x4f76, a, 3);
    if (CATCHER_TIE != 0) catcher_warp++;
  }
  if (catcher_warp > 0 && player_race(pkt_owner) == 6) {
    /* the race 6 owner of the packet learns the starbase design that catches it */
    const uint16_t seen = (uint16_t)(1u << ((packet_id >> 9) & 0xf));
    const uint32_t sb_designs = drd32((uint16_t)(DS_SB_DESIGNS + (uint16_t)(PLANET(planet_seg, planet_off)->owner << 2)));
    design_seen_by(FAR_REC(struct ShipDesign, FP_SEG(sb_designs),
                           (uint16_t)(FP_OFF(sb_designs) +
                                      (uint16_t)((PLANET(planet_seg, planet_off)->starbase & PLANET_STARBASE_DESIGN_MASK) * sizeof(struct ShipDesign)))),
                   seen);
  }
  const bool race6 = player_race(pkt_owner) == 6;
  int16_t catch_power = (int16_t)(catcher_warp * catcher_warp);
  const int16_t packet_power = (int16_t)(packet_warp * packet_warp);
  if (player_race(PLANET(planet_seg, planet_off)->owner) == 7) catch_power = (int16_t)(catch_power / 2);
  int16_t reference; /* the catcher's warp, or its power when the packet is stronger */
  int16_t caught;    /* the part caught, in thousandths */
  if (packet_power <= catch_power) {
    reference = catcher_warp;
    caught = 1000;
  } else if (catcher_warp > 0) {
    reference = catch_power;
    caught = (int16_t)(((int32_t)catch_power * 1000) / packet_power);
  } else {
    reference = catcher_warp;
    caught = 0;
  }
  const int16_t uncaught = (int16_t)(1000 - caught);
  for (uint16_t k = 0; k < 3; k++)
    PACKET_LEAK(k) = (uint16_t)((int32_t)((int32_t)(int16_t)object_word_at(seg, off, (uint16_t)(8 + 2 * k)) * (int32_t)uncaught) / 1000);
  const int16_t landing = (int16_t)((int16_t)((1000 - caught) / 9) + caught); /* what is caught and a ninth of the rest land */
  int32_t delivered = 0;
  for (uint16_t k = 0; k < 3; k++) {
    if ((int16_t)object_word_at(seg, off, (uint16_t)(8 + 2 * k)) < 0) set_object_word_at(seg, off, (uint16_t)(8 + 2 * k), 0);
    const int16_t amount = (int16_t)object_word_at(seg, off, (uint16_t)(8 + 2 * k));
    delivered += amount;
    PLANET(planet_seg, planet_off)->surface_amounts_maybe[k] += (uint32_t)(((int32_t)amount * (int32_t)landing) / 1000);
  }
  const uint16_t pid = PLANET(planet_seg, planet_off)->id;
  if (caught != 1000) {
    const int32_t damage = (int32_t)((uint32_t)(int32_t)(int16_t)(packet_power - reference) * (uint32_t)delivered) / 160;
    if (race6) {
      uint16_t axis_tag = 0; /* the axis in the high byte of the value in the messages */
      for (uint16_t axis = 0; axis < 3; axis++, axis_tag = (uint16_t)(axis_tag + 0x100)) {
        int16_t chunks = 0, gains = 0; /* 100-mineral chunks of the part not caught, and the ones of them that also gain */
        if ((int16_t)PACKET_LEAK(axis) > 0) {
          do {
            const int16_t roll = (int16_t)rand_below(200);
            int16_t share = (int16_t)PACKET_LEAK(axis);
            if (share > 100) share = 100;
            if (share > roll) {
              chunks++;
              if (rand_below(10) == 0) gains++;
            }
            PACKET_LEAK(axis) = (uint16_t)(PACKET_LEAK(axis) - 100);
          } while ((int16_t)PACKET_LEAK(axis) > 0);
        }
        packet_shifts_environment(planet_seg, planet_off, pkt_owner, axis, chunks, gains, axis_tag);
      }
    }
    /* the attack on the planet */
    if (PLANET(planet_seg, planet_off)->owner == 0xffff) return OBJECT_DELETE;
    {
      const uint16_t a[4] = {planet_off, planet_seg, (uint16_t)(rt_bp - 0x28), 0};
      (void)far_call(7, 0x0262, a, 4);
    }
    const int32_t kills = fp80_to_i32_ftol(fp80_imul_i32(fp80_from_f32(PLANET_HABITABILITY_F32), damage));
    if (kills != 0 && player_race(PLANET(planet_seg, planet_off)->owner) != 8) {
      const uint32_t pop = PLANET(planet_seg, planet_off)->surface_amounts_maybe[3];
      int32_t pop_killed = (int32_t)(pop * (uint32_t)kills) / 1000;
      if (pop_killed < kills) pop_killed = kills;
      if ((int32_t)pop <= pop_killed || pop_killed < 0) { /* nobody is left */
        send_message2(PLANET(planet_seg, planet_off)->owner, 0xda, pid, pid, pkt_owner);
        const uint16_t a[2] = {planet_off, planet_seg};
        (void)far_call(9, 0x553a, a, 2);
        return OBJECT_DELETE;
      }
      const uint16_t installations = planet_installations_maybe(planet_seg, planet_off);
      int32_t destroyed = (int32_t)((uint32_t)installations * (uint32_t)kills) / 1000;
      if (destroyed == 0 && installations != 0) destroyed = (int32_t)rand_below(20) < kills ? 1 : 0;
      if (kills / 20 > destroyed) destroyed = kills / 20;
      if ((int32_t)installations < destroyed) destroyed = installations;
      if (destroyed == 0) {
        SCRATCH_MESSAGE = reference == 0 ? 0xd8 : 0xd6;
        send_message(PLANET(planet_seg, planet_off)->owner, SCRATCH_MESSAGE, pid, pid, (uint16_t)delivered, (uint16_t)((uint32_t)delivered >> 16),
                     pkt_owner, (uint16_t)pop_killed, 0, 0);
      } else {
        SCRATCH_MESSAGE = reference == 0 ? 0xd9 : 0xd7;
        send_message(PLANET(planet_seg, planet_off)->owner, SCRATCH_MESSAGE, pid, pid, (uint16_t)delivered, (uint16_t)((uint32_t)delivered >> 16),
                     pkt_owner, (uint16_t)pop_killed, (uint16_t)destroyed, 0);
        const uint16_t word = object_word_at(planet_seg, planet_off, 0x18);
        set_object_word_at(planet_seg, planet_off, 0x18, (uint16_t)(word ^ (uint16_t)(((uint16_t)(word - (uint16_t)destroyed) ^ word) & 0xfff)));
      }
      PLANET(planet_seg, planet_off)->surface_amounts_maybe[3] -= (uint32_t)pop_killed;
      return OBJECT_DELETE;
    }
  }
  /* everything landed (or no damage): the owner of the planet is told */
  send_message(PLANET(planet_seg, planet_off)->owner, reference > 0 ? 0xd5 : 0x146, pid, pid, pkt_owner, (uint16_t)delivered,
               (uint16_t)((uint32_t)delivered >> 16), 0, 0, 0);
  return OBJECT_DELETE;
}

/* Mineral packet: it moves towards its planet (warp * warp units, half of that in each of the two phases) and, when it gets there,
 * may be caught by 23:0x4166 (which can delete it) or lands (packet_hits_planet). An empty packet is deleted. */
static enum ObjectOutcome packet_step(uint16_t seg, uint16_t off, uint16_t phase, bool *moved_any)
{
  const uint8_t flags7 = map_object_byte7(seg, off);
  if ((flags7 & MAPOBJ_PACKET_WARP_MASK) == 0) return OBJECT_KEEPS;
  if (phase != 0 && (flags7 & 0x40) != 0) return OBJECT_KEEPS; /* it already moved in the first phase */
  if (object_word_at(seg, off, 8) == 0 && object_word_at(seg, off, 0xa) == 0 && object_word_at(seg, off, 0xc) == 0) return OBJECT_DELETE;
  set_object_word_at(seg, off, 6, (uint16_t)(object_word_at(seg, off, 6) | 0x4000));
  *moved_any = true;
  const uint16_t warp = (uint16_t)(((map_object_byte7(seg, off) & MAPOBJ_PACKET_WARP_MASK) >> 2) + 4);
  int16_t speed = (int16_t)(warp * warp);
  if (phase != 0) speed = (int16_t)(speed >> 1);
  const uint32_t target = drd32((uint16_t)(DS_PLANET_XY + (uint16_t)((object_word_at(seg, off, 6) & 0x3ff) << 2)));
  int16_t distance;
  if (!object_step_toward(seg, off, target, speed, &distance)) {
    if (phase == 0) return OBJECT_KEEPS;
    if (map_object_kind(FAR_REC(struct MapObject, seg, off)->owner_kind_bits) != MAPOBJ_KIND_PACKET) return OBJECT_KEEPS;
    const uint16_t a[3] = {off, seg, 0x32};
    return far_call(23, 0x4166, a, 3) != 0 ? OBJECT_GONE : OBJECT_KEEPS; /* a catcher or a pirate along the way */
  }
  /* it has arrived: how much of the last step it needed, in percent (half that in the second phase), for the catcher */
  const int16_t needed = MulDiv(distance, 100, speed);
  int16_t percent = needed < 0 ? 0 : needed;
  if (percent > 100) percent = 100;
  if (phase != 0) percent = (int16_t)(percent >> 1);
  {
    const uint16_t a[3] = {off, seg, (uint16_t)percent};
    if (far_call(23, 0x4166, a, 3) != 0) return OBJECT_GONE;
  }
  return packet_hits_planet(seg, off);
}

/* move_map_objects: the turn's movement of the map objects that move (mineral packets, wormholes, the mystery trader).
 * 22:0x0f6e FUN_10a8_0f6e, FAR CDECL (phase [BP+6]: 0 or 1), 3467 bytes (ENTER 0x70,0; PUSH DI; PUSH SI), x87.
 * Walks the map objects (DS:0x188E, g_map_object_count records of 0x12 bytes; the kind is the top three bits of the
 * id word):
 *   0x4000 wormhole (phase 1)     its level (33:0x0742) against a roll 8:0x8730(100) decides whether it jumps to a new place
 *                                 (up to 100 tries, 33:0x0456 grades the spot) or only ages
 *   0x6000 mystery trader (phase 0) is announced and relocated now and then (6:0x7328 to every player) and moves towards a
 *                                 target on the border of the universe; there is only one
 *   0x2000 mineral packet         moves towards its target planet (id and warp in the word at +6, positions from the
 *                                 table at DS:0x31BA) by warp * warp units; one that arrives is caught by the planet's mass
 *                                 drivers or hits the planet: damage to the planet's colonists and installations, the minerals
 *                                 added to the planet, messages 0xd5 / 0x146 / 0xda ... (see packet_hits_planet)
 * A deleted object (33:0x015a) or one a callee deleted lets the walk go on with the object that moved into its place; at the end
 * 7:0x42c8 runs once when anything moved. */
DECOMP(22, 0x0f6e, move_map_objects, FAR_CDECL)
void move_map_objects(uint16_t phase)
{
  FRAME_ENTER_REGS(0x70, 2);
  bool moved_any = false;
  const uint16_t seg = g_map_objects_segment_maybe;
  uint16_t cur = g_map_objects;
  uint16_t end = (uint16_t)(cur + (uint16_t)((int16_t)g_map_object_count * (int16_t)sizeof(struct MapObject)));
  while (cur < end) {
    const uint16_t kind = (uint16_t)(FAR_REC(struct MapObject, seg, cur)->id_kind & OBJECT_KIND_WORD_MASK);
    enum ObjectOutcome outcome = OBJECT_KEEPS;
    if (kind == OBJECT_KIND_WORD_WORMHOLE_maybe && phase != 0) wormhole_step(seg, cur);
    else if (kind == OBJECT_KIND_WORD_TRADER_maybe && phase == 0) outcome = trader_step(seg, cur, &moved_any);
    else if (kind == OBJECT_KIND_WORD_PACKET) outcome = packet_step(seg, cur, phase, &moved_any);
    if (outcome == OBJECT_DELETE) {
      const uint16_t a[2] = {cur, seg};
      (void)far_call(33, 0x015a, a, 2);
    }
    if (outcome != OBJECT_KEEPS) {
      cur = (uint16_t)(cur - sizeof(struct MapObject));
      end = (uint16_t)(end - sizeof(struct MapObject));
    }
    cur = (uint16_t)(cur + sizeof(struct MapObject));
  }
  if (moved_any) (void)CALL_ORIG(7, 0x42c8, FAR_CDECL, 0);
  FRAME_LEAVE();
}
/* ---- 22:0x1f18 move_fleets ---- */

/* The bookkeeping fields at the end of a fleet record that 22:0x1f18 uses (struct Fleet has them as padding, fld_74 and the far
 * pointer unknown_68): a far pointer to the fleet that this one follows (found by 7:0x14a8, +0x68), the distance it may still move
 * in the passes (+0x6c), the distance already moved (+0x6e), the fuel used so far (+0x70), the heading vector (two bytes at +0x74,
 * the direction of the move plus 0x7f) and the byte at +0x76 (low nibble: the warp, bit 4: moved this turn). */
static inline uint32_t fleet_followed_maybe(uint16_t seg, uint16_t off) { return rd32(seg, (uint16_t)(off + 0x68)); }
static inline void set_fleet_followed_maybe(uint16_t seg, uint16_t off, uint32_t v) { wr32(seg, (uint16_t)(off + 0x68), v); }
static inline uint16_t fleet_step_left(uint16_t seg, uint16_t off) { return rd16(seg, (uint16_t)(off + 0x6c)); }
static inline void set_fleet_step_left(uint16_t seg, uint16_t off, uint16_t v) { wr16(seg, (uint16_t)(off + 0x6c), v); }
static inline uint16_t fleet_step_done(uint16_t seg, uint16_t off) { return rd16(seg, (uint16_t)(off + 0x6e)); }
static inline void set_fleet_step_done(uint16_t seg, uint16_t off, uint16_t v) { wr16(seg, (uint16_t)(off + 0x6e), v); }
static inline uint32_t fleet_fuel_used_maybe(uint16_t seg, uint16_t off) { return rd32(seg, (uint16_t)(off + 0x70)); }
static inline void set_fleet_fuel_used_maybe(uint16_t seg, uint16_t off, uint32_t v) { wr32(seg, (uint16_t)(off + 0x70), v); }
/* The byte at +0x76: low nibble the warp of the last move, bit 4 set when the fleet moved. */
static inline uint8_t fleet_move_state(uint16_t seg, uint16_t off) { return rd8(seg, (uint16_t)(off + 0x76)); }
static inline void set_fleet_move_state(uint16_t seg, uint16_t off, uint8_t v) { wr8(seg, (uint16_t)(off + 0x76), v); }
/* The colonists of the fleet: the fourth cargo dword (Fleet.cargo[3], +0x58). */
static inline uint32_t fleet_colonists(uint16_t seg, uint16_t off) { return FLEET(seg, off)->cargo[3]; }

/* Bits of Fleet.flags that 22:0x1f18 uses (struct Fleet names bit 2, empty): 0x08 the fleet was handled in this pass, 0x20 it still has
 * to move this turn, 0x04 empty, 0x40 hit a minefield (set by 22:0x30b6). */
enum { FLEET_FLAG_HANDLED_maybe = 0x08, FLEET_FLAG_TO_MOVE_maybe = 0x20 };
/* The warp, task and target kind of a waypoint (Waypoint.flags: task in bits 0 to 3, warp in bits 4 to 7, target kind in bits 8 to 11). */
static inline uint16_t waypoint_warp(const struct Waypoint *w) { return (uint16_t)((w->flags >> 4) & 0xf); }
static inline uint16_t waypoint_task(const struct Waypoint *w) { return (uint16_t)(w->flags & 0xf); }
static inline uint16_t waypoint_target_kind(const struct Waypoint *w) { return (uint16_t)((w->flags >> 8) & 0xf); }
/* Waypoint n of a fleet: the list is a WaypointListHdr followed by the waypoints. */
static inline struct Waypoint *fleet_waypoint(uint16_t fseg, uint16_t foff, uint16_t n)
{
  const uint32_t list = FLEET(fseg, foff)->waypoints;
  return FAR_REC(struct Waypoint, FP_SEG(list), (uint16_t)(FP_OFF(list) + sizeof(struct WaypointListHdr) + n * sizeof(struct Waypoint)));
}
/* Is the player's trait n set (28:0x2230(player row, n))? */
static bool player_trait(uint16_t player, uint16_t n)
{
  return (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player))), n) != 0;
}
/* The planet record of a planet id (7:0x01e4), the map object of an object id (7:0x01a0), the fleet of a fleet id (7:0x14a8). */
static uint32_t planet_by_id(uint16_t id) { return (uint32_t)CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, id); }
static uint32_t map_object_by_id(uint16_t id) { return (uint32_t)CALL_ORIG(7, 0x01a0, FAR_CDECL, 1, id); }
static uint32_t fleet_by_id(uint16_t id) { return (uint32_t)CALL_ORIG(7, 0x14a8, FAR_CDECL, 1, id); }
/* The distance of two points as the game computes it (7:0x28ea gives a double at a DGROUP address). */
static double points_distance(uint32_t from, uint32_t to)
{
  const uint16_t at = (uint16_t)CALL_ORIG(7, 0x28ea, FAR_CDECL, 4, (uint16_t)from, (uint16_t)(from >> 16), (uint16_t)to, (uint16_t)(to >> 16));
  return ds_double(at);
}
/* The fuel figure 10:0x6312 computes for a fleet: (fleet, 0, warp, distance dword, flag), the result in DX:AX. With the warp 0xffff it takes the
 * warp of the fleet's next waypoint; the distance 0xffffffff means any distance. */
static uint32_t fleet_fuel_figure(uint32_t fleet, uint16_t warp, uint32_t distance, uint16_t flag)
{
  return (uint32_t)CALL_ORIG(10, 0x6312, FAR_CDECL, 7, FP_OFF(fleet), FP_SEG(fleet), 0, warp, (uint16_t)distance, (uint16_t)(distance >> 16), flag);
}
/* The fleet's maximum fuel: 10:0x292a(fleet, 1). */
static uint32_t fleet_max_fuel(uint32_t fleet) { return (uint32_t)CALL_ORIG(10, 0x292a, FAR_CDECL, 3, FP_OFF(fleet), FP_SEG(fleet), 1); }

/* The message ids that 22:0x1f18 sends (6:0x7328 and 6:0x7344). The wording is in the game's string table, which was not read here:
 * the names say what the branch means. */
enum {
  MSG_FLEET_IDLE_maybe = 0x102,           /* an AI player's fleet does not move this turn (1 in 4) */
  MSG_ENGINES_FAIL_maybe = 0xf2,          /* a fast fleet's engines fail (trait 8, 1 in 10) */
  MSG_GATE_NO_SOURCE_maybe = 0xde,        /* the fleet is not at a planet with a gate */
  MSG_GATE_NOT_YOURS_maybe = 0xe6,        /* the gate at the fleet's planet belongs to somebody else */
  MSG_GATE_NO_DEST_PLANET_maybe = 0x147,  /* no planet at the gate destination */
  MSG_GATE_NO_DEST_GATE_maybe = 0xe2,     /* the destination planet has no gate */
  MSG_GATE_DEST_NOT_YOURS_maybe = 0xe5,   /* the gate at the destination belongs to somebody else */
  MSG_GATE_COLONISTS_BLOCKED_maybe = 0x15e,
  MSG_CARGO_LEFT_AT_GATE_maybe = 0xec,    /* minerals left at the planet (0xed colonists, 0xee both) */
  MSG_COLONISTS_DECAY_maybe = 0xc1,
  MSG_HYPER_ALL_LOST_maybe = 0xe1,
  MSG_HYPER_ONE_LOST_maybe = 0xdf,
  MSG_HYPER_SOME_LOST_maybe = 0xe0,
  MSG_OUT_OF_FUEL_SLOWED_maybe = 0x8b,
  MSG_OUT_OF_FUEL_STOPPED_maybe = 0x27,
  MSG_COLONISTS_LOST_TRAVELLING_maybe = 0x74,
  MSG_FUEL_USE_maybe = 0xf3
};

/* Stack slots of 22:0x1f18 that a callee gets the address of. */
#define RANGE_OUT_WORD L16(-0x2a)                      /* 22:0x30b6 gets it as the word with the distance the fleet moves */
#define PLANET_SEARCH_OUT L16(-0x14)                   /* 7:0x294e fills a record here; the planet id is the word at BP-0xc */
#define PLANET_SEARCH_PLANET L16(-0xc)
#define FLEET_COPY2 BP_LOCAL(struct Fleet, -0x16c)     /* the fleet that jumps at warp 10; 10:0x6c1c gets its address */
#define FLEET_LOST2 BP_LOCAL(struct Fleet, -0xf0)      /* the ships it loses */

/* A message with two values to a fleet's owner about the fleet: (owner, id, fleet id | 0x8000, v0, v1) through 6:0x7328. */
static void fleet_message2(uint16_t owner, uint16_t id, uint16_t fleet_id, uint16_t v0, uint16_t v1)
{
  send_message2(owner, id, (uint16_t)(fleet_id | 0x8000), v0, v1);
}
/* The relation byte of the owner of a planet towards a fleet's owner is 1: the planet's owner is at peace with the fleet's owner. */
static bool at_peace_with(uint16_t planet_owner, uint16_t fleet_owner) { return player_relation(planet_owner, fleet_owner) == 1; }
/* The planet whose position is (x, y): its index in the planet position table (DS:0x31ba), or the planet count. */
static uint16_t planet_index_at(uint32_t xy)
{
  uint16_t i = 0;
  for (; (int16_t)i < (int16_t)g_planet_count_in_game; i++)
    if (drd16((uint16_t)(DS_PLANET_XY + (uint16_t)(i << 2))) == (uint16_t)xy && drd16((uint16_t)(DS_PLANET_XY + 2 + (uint16_t)(i << 2))) == (uint16_t)(xy >> 16)) break;
  return i;
}

/* A fleet that goes through a stargate (the second waypoint has warp 11 or more): the gate of the planet it is at (or its own gate
 * item, 7:0x4bde) and the gate at the destination planet must be usable; cargo that the fleet carries is left at the planet; the jump
 * itself is 16:0x0962 (apply_stargate_losses). Returns false when the fleet does not go (a message says why). */
static bool fleet_enters_stargate(uint16_t fseg, uint16_t foff, const struct Waypoint *wp0, const struct Waypoint *wp1)
{
  const uint16_t owner = FLEET(fseg, foff)->owner;
  const uint16_t fleet_id = FLEET(fseg, foff)->id;
  bool own_gate = false;
  g_ui_flags_a_maybe = (uint8_t)(g_ui_flags_a_maybe & 0xbf);
  uint32_t where = wp0->x | ((uint32_t)wp0->y << 16); /* the place named in the messages: the position, or (0xffff, planet id) */
  uint32_t src_planet = 0;
  uint16_t src_gate = 0xffff; /* the gate item of the planet the fleet is at (7:0x0af8) */
  if (waypoint_target_kind(wp0) == 1) {
    where = 0xffffu | ((uint32_t)wp0->target_id << 16);
    src_planet = planet_by_id(wp0->target_id);
    {
      const uint16_t a[2] = {FP_OFF(src_planet), FP_SEG(src_planet)};
      src_gate = (uint16_t)far_call(7, 0x0af8, a, 2);
    }
  }
  if (src_gate == 0xffff) {
    const uint16_t a[2] = {foff, fseg};
    if ((uint16_t)far_call(7, 0x4bde, a, 2) == 0) {
      send_message(owner, MSG_GATE_NO_SOURCE_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, (uint16_t)where, (uint16_t)(where >> 16), 0, 0, 0, 0);
      return false;
    }
  } else if (PLANET(FP_SEG(src_planet), FP_OFF(src_planet))->owner != owner &&
             !at_peace_with(PLANET(FP_SEG(src_planet), FP_OFF(src_planet))->owner, owner)) {
    const uint16_t pid = PLANET(FP_SEG(src_planet), FP_OFF(src_planet))->id;
    send_message(owner, MSG_GATE_NOT_YOURS_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, pid, pid, 0, 0, 0, 0);
    return false;
  }
  /* the destination */
  const uint32_t target = wp1->x | ((uint32_t)wp1->y << 16);
  uint32_t dest_planet;
  if (waypoint_target_kind(wp1) == 1) {
    where = 0xffffu | ((uint32_t)wp1->target_id << 16);
    dest_planet = planet_by_id(wp1->target_id);
  } else {
    const uint16_t index = planet_index_at(target);
    if ((int16_t)index >= (int16_t)g_planet_count_in_game) {
      send_message(owner, MSG_GATE_NO_DEST_PLANET_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, (uint16_t)target, (uint16_t)(target >> 16), 0, 0, 0, 0);
      return false;
    }
    where = 0xffffu | ((uint32_t)index << 16);
    dest_planet = planet_by_id(index);
  }
  uint16_t dest_gate;
  {
    const uint16_t a[2] = {FP_OFF(dest_planet), FP_SEG(dest_planet)};
    dest_gate = (uint16_t)far_call(7, 0x0af8, a, 2);
  }
  const uint16_t dest_id = PLANET(FP_SEG(dest_planet), FP_OFF(dest_planet))->id;
  const uint16_t dest_owner = PLANET(FP_SEG(dest_planet), FP_OFF(dest_planet))->owner;
  if (dest_gate == 0xffff) {
    send_message(owner, MSG_GATE_NO_DEST_GATE_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, dest_id, (uint16_t)where, (uint16_t)(where >> 16), 0, 0, 0);
    return false;
  }
  if (dest_owner != owner && !at_peace_with(dest_owner, owner)) {
    send_message(owner, MSG_GATE_DEST_NOT_YOURS_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, dest_id, dest_id, dest_id, 0, 0, 0);
    return false;
  }
  if (src_gate == 0xffff) { /* the fleet carries the gate itself */
    own_gate = true;
    src_gate = dest_gate;
  }
  if (!own_gate && player_race(owner) != 7) {
    /* the cargo stays behind at the planet it leaves */
    const uint16_t src_seg = FP_SEG(src_planet), src_off = FP_OFF(src_planet);
    if ((int32_t)fleet_colonists(fseg, foff) > 0 && PLANET(src_seg, src_off)->owner != owner) {
      fleet_message2(owner, MSG_GATE_COLONISTS_BLOCKED_maybe, fleet_id, fleet_id, PLANET(src_seg, src_off)->id);
      return false;
    }
    uint32_t minerals = 0;
    for (uint16_t i = 0; (int16_t)i <= 2; i++) {
      if (FLEET(fseg, foff)->cargo[i] != 0) {
        minerals += FLEET(fseg, foff)->cargo[i];
        PLANET(src_seg, src_off)->surface_amounts_maybe[i] += FLEET(fseg, foff)->cargo[i];
        FLEET(fseg, foff)->cargo[i] = 0;
      }
    }
    const uint32_t colonists = fleet_colonists(fseg, foff);
    PLANET(src_seg, src_off)->surface_amounts_maybe[3] += FLEET(fseg, foff)->cargo[3];
    FLEET(fseg, foff)->cargo[3] = 0;
    const uint16_t src_id = PLANET(src_seg, src_off)->id, src_owner = PLANET(src_seg, src_off)->owner;
    if (colonists != 0 && minerals != 0) {
      send_message(owner, 0xee, (uint16_t)(fleet_id | 0x8000), fleet_id, (uint16_t)colonists, (uint16_t)(colonists >> 16), (uint16_t)minerals,
                   (uint16_t)(minerals >> 16), src_id, 0);
      if (src_owner != owner)
        send_message(src_owner, 0xee, src_id, fleet_id, (uint16_t)colonists, (uint16_t)(colonists >> 16), (uint16_t)minerals, (uint16_t)(minerals >> 16), src_id, 0);
    } else if (colonists != 0) {
      send_message(owner, 0xed, (uint16_t)(fleet_id | 0x8000), fleet_id, (uint16_t)colonists, (uint16_t)(colonists >> 16), src_id, 0, 0, 0);
      if (src_owner != owner)
        send_message(src_owner, 0xed, src_id, fleet_id, (uint16_t)colonists, (uint16_t)(colonists >> 16), src_id, 0, 0, 0);
    } else if (minerals != 0) {
      send_message(owner, MSG_CARGO_LEFT_AT_GATE_maybe, (uint16_t)(fleet_id | 0x8000), fleet_id, (uint16_t)minerals, (uint16_t)(minerals >> 16), src_id, 0, 0, 0);
      if (src_owner != owner)
        send_message(src_owner, MSG_CARGO_LEFT_AT_GATE_maybe, src_id, fleet_id, (uint16_t)minerals, (uint16_t)(minerals >> 16), src_id, 0, 0, 0);
    }
  }
  /* the jump */
  const uint16_t distance = (uint16_t)fp80_to_i32_ftol(fp80_from_f64(points_distance(wp0->x | ((uint32_t)wp0->y << 16), target)));
  {
    const uint16_t a[5] = {foff, fseg, src_gate, dest_gate, distance};
    if ((uint16_t)far_call(16, 0x0962, a, 5) == 0) return false;
  }
  FLEET(fseg, foff)->flags = (uint8_t)(FLEET(fseg, foff)->flags & 0xdf);
  {
    const uint16_t a[2] = {foff, fseg};
    (void)far_call(16, 0x133e, a, 2);
  }
  return true;
}

/* A fleet whose second waypoint has warp 10 risks its ships: every ship of a design whose engine is not one of the safe ones (items 7, 8, 9,
 * 14 and 15) is lost with a chance of 1 in 10. A fleet that loses them all is gone (flag 4), else the lost ships are removed (10:0x6c1c).
 * Returns false when the whole fleet is lost. */
static bool fleet_jumps_at_warp_10(uint16_t fseg, uint16_t foff)
{
  const uint16_t owner = FLEET(fseg, foff)->owner;
  const uint32_t designs = player_designs(owner);
  memcpy(&FLEET_COPY2, FLEET(fseg, foff), sizeof(struct Fleet));
  bool all_lost = true;
  int16_t lost_total = 0;
  memset(&FLEET_LOST2, 0, sizeof(struct Fleet));
  for (uint16_t slot = 0; (int16_t)slot < FLEET_DESIGN_SLOTS; slot++) {
    if (FLEET_COPY2.ship_count[slot] != 0) {
      const uint8_t engine = design_of(designs, slot)->slots[0].item;
      if (engine != 7 && engine != 9 && engine != 0xe && engine != 0xf && engine != 8) {
        int16_t lost = 0;
        for (uint16_t n = FLEET_COPY2.ship_count[slot]; n != 0; n--)
          if (rand_below(10) == 0) lost++;
        if (lost > 0) {
          lost_total = (int16_t)(lost_total + lost);
          FLEET_COPY2.ship_count[slot] = (uint16_t)(FLEET_COPY2.ship_count[slot] - lost);
          FLEET_LOST2.ship_count[slot] = (uint16_t)lost;
        }
      }
    }
    if ((int16_t)FLEET_COPY2.ship_count[slot] > 0) all_lost = false;
  }
  if (all_lost) {
    FLEET(fseg, foff)->flags |= FLEET_FLAG_EMPTY;
    fleet_message2(owner, MSG_HYPER_ALL_LOST_maybe, FLEET(fseg, foff)->id, FLEET(fseg, foff)->id, 0);
    return false;
  }
  if (lost_total > 0) {
    FLEET_LOST2.owner = FLEET_COPY2.owner;
    FLEET_LOST2.flags |= FLEET_FLAG_EMPTY;
    FLEET_LOST2.version = 7;
    (void)CALL_ORIG(10, 0x6c1c, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x16c), (uint16_t)(rt_bp - 0xf0));
    memcpy(FLEET(fseg, foff), &FLEET_COPY2, sizeof(struct Fleet));
    if (lost_total == 1) fleet_message2(owner, MSG_HYPER_ONE_LOST_maybe, FLEET(fseg, foff)->id, FLEET(fseg, foff)->id, 0);
    else fleet_message2(owner, MSG_HYPER_SOME_LOST_maybe, FLEET(fseg, foff)->id, (uint16_t)lost_total, FLEET(fseg, foff)->id);
  }
  return true;
}


/* The low byte of Player.fld_54 (DS:0x5ab8 + 0xc0 * player): bit 2 marks the computer players that 22:0x1f18 staggers. */
static inline uint8_t player_flags_byte(uint16_t player)
{
  return (uint8_t)FAR_REC(struct Player, MEM_SEL_DGROUP, (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player))))->fld_54;
}
/* Signed bytes of a player record at +0x15 and +0x18 (struct Player lists them as padding): 22:0x1f18 sums them for the colonists that die
 * on a long trip. */
static inline int8_t player_travel_byte_maybe(uint16_t player, uint16_t off)
{
  return (int8_t)drd8((uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)) + off));
}

/* The colonists that die on the trip (DS:0x790 bit 6 set, first two passes): the rate comes from two bytes of the player record, the loss
 * is that percent of the colonists, at least 1 and at most all of them. */
static void colonists_die_travelling(uint16_t fseg, uint16_t foff)
{
  const uint16_t owner = FLEET(fseg, foff)->owner;
  const int16_t sum = (int16_t)(player_travel_byte_maybe(owner, 0x18) + player_travel_byte_maybe(owner, 0x15));
  if (sum >= 0xaa) return;
  if ((uint8_t)player_travel_byte_maybe(owner, 0x18) == 0xff) return;
  const int16_t average = (int16_t)(sum / 2);
  const int16_t rate = (int16_t)((int16_t)(0x56 - average) >> 1);
  const int32_t colonists = (int32_t)fleet_colonists(fseg, foff);
  int32_t loss = (int32_t)((uint32_t)colonists * (uint32_t)(int32_t)rate) / 100;
  if (loss < 1) loss = 1;
  if (colonists < loss) loss = colonists;
  fleet_message2(owner, MSG_COLONISTS_LOST_TRAVELLING_maybe, FLEET(fseg, foff)->id, (uint16_t)loss, FLEET(fseg, foff)->id);
  FLEET(fseg, foff)->cargo[3] -= (uint32_t)loss;
}

/* One fleet in one pass of 22:0x1f18. */
/* The stack slot BP-0x56 of move_fleets: whether the fleet before had enough fuel. It is read before it is written for a stargate
 * fleet or the first fleet, so it stays a frame slot and the first read sees what the stack held. */
#define ENOUGH_FUEL_KEPT L16(-0x56)
static void move_one_fleet(uint32_t fleet, uint16_t pass, bool *all_done)
{
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  const uint16_t owner = FLEET(fseg, foff)->owner;
  const uint16_t fleet_id = FLEET(fseg, foff)->id;
  if (pass == 0) {
    wr32(fseg, (uint16_t)(foff + 0x74), 0);
    FLEET(fseg, foff)->flags |= FLEET_FLAG_TO_MOVE_maybe;
  }
  if ((FLEET(fseg, foff)->flags & FLEET_FLAG_EMPTY) != 0) return;
  if ((int16_t)pass > 0 && (FLEET(fseg, foff)->flags & FLEET_FLAG_HANDLED_maybe) != 0) return;
  FLEET(fseg, foff)->flags |= FLEET_FLAG_HANDLED_maybe;
  /* a fleet that is at a task of waypoint 0 (1 or 6) or has no second waypoint with a warp does not move */
  struct Waypoint *wp0 = fleet_waypoint(fseg, foff, 0);
  if (waypoint_task(wp0) == 1 || waypoint_task(wp0) == 6) return;
  if ((int16_t)FLEET(fseg, foff)->waypoint_count <= 1) return;
  struct Waypoint *wp1 = fleet_waypoint(fseg, foff, 1);
  if ((wp1->flags & 0xf0) == 0) return;
  /* computer players: staggered by turn, and now and then idle */
  if ((player_flags_byte(owner) & 4) != 0) {
    if (g_current_turn > 10 && (uint8_t)(owner & 7) == (uint8_t)(g_current_turn & 7)) return;
    if (rand_below(4) == 0) {
      send_message2(owner, MSG_FLEET_IDLE_maybe, 0xfffb, fleet_id, 0);
      return;
    }
  }
  /* the engines may fail at warp 7 and more */
  if (pass == 0 && (wp1->flags & 0xf0) > 0x60 && (wp1->flags & 0xf0) != 0xb0 && player_trait(owner, 8) && rand_below(10) == 0) {
    fleet_message2(owner, MSG_ENGINES_FAIL_maybe, fleet_id, fleet_id, 0);
    return;
  }

  uint32_t start;
  uint32_t target;
  int32_t move_left = 0;   /* the distance the fleet moves in this pass */
  bool arrives;
  bool out_of_fuel = false; /* the move was slowed down because the fuel is short */
  if ((wp1->flags & 0xf0) >= 0xb0) { /* a stargate */
    if (!fleet_enters_stargate(fseg, foff, wp0, wp1)) return;
    target = wp1->x | ((uint32_t)wp1->y << 16);
    arrives = true;
    (void)start;
  } else {
    start = FLEET(fseg, foff)->xy;
    if ((int16_t)pass > 0 && (wp1->flags & 0x2000) == 0) { /* it follows a fleet: its waypoint is that fleet's position */
      const uint32_t followed = fleet_followed_maybe(fseg, foff);
      wp1->x = FLEET(FP_SEG(followed), FP_OFF(followed))->x;
      wp1->y = FLEET(FP_SEG(followed), FP_OFF(followed))->y;
    }
    target = wp1->x | ((uint32_t)wp1->y << 16);
    int32_t fuel_cap = (int32_t)fleet_fuel_figure(fleet, 0xffff, 0xffffffffu, 1); /* the most it may move with its fuel */
    const int32_t fuel_needed = (int32_t)fleet_fuel_figure(fleet, 0xffff, 0xffffffffu, 0);
    const bool enough_fuel = (int32_t)FLEET(fseg, foff)->fuel >= fuel_needed;
    ENOUGH_FUEL_KEPT = enough_fuel ? 1 : 0;
    if (enough_fuel) {
      const int32_t warp_range = (int32_t)(waypoint_warp(wp1) * waypoint_warp(wp1));
      if (warp_range >= fuel_cap) fuel_cap = warp_range;
    }
    if (pass == 0) {
      /* race 8: colonists die off */
      if ((int32_t)fleet_colonists(fseg, foff) > 10 && player_race(owner) == 8) {
        const int32_t decay = (int32_t)((uint32_t)((int32_t)fleet_colonists(fseg, foff) + 11) * 3u) / 100;
        if (decay > 0) {
          FLEET(fseg, foff)->cargo[3] -= (uint32_t)decay;
          send_message(owner, MSG_COLONISTS_DECAY_maybe, (uint16_t)(fleet_id | 0x8000), (uint16_t)decay, (uint16_t)((uint32_t)decay >> 16), fleet_id, 0, 0, 0, 0);
        }
      }
      if ((wp1->flags & 0xf0) == 0xa0 && !fleet_jumps_at_warp_10(fseg, foff)) return;
      move_left = (int32_t)(waypoint_warp(wp1) * waypoint_warp(wp1));
      if (waypoint_target_kind(wp1) == 2) { /* it follows a fleet: wait for it to move first */
        const uint32_t followed = fleet_by_id(wp1->target_id);
        set_fleet_followed_maybe(fseg, foff, followed);
        if (followed != 0) {
          *all_done = false;
          FLEET(fseg, foff)->flags = (uint8_t)(FLEET(fseg, foff)->flags & 0xf7);
          set_fleet_step_left(fseg, foff, (uint16_t)move_left);
          set_fleet_step_done(fseg, foff, 0);
          set_fleet_fuel_used_maybe(fseg, foff, 0);
          return;
        }
      }
    } else {
      const uint32_t followed = fleet_followed_maybe(fseg, foff);
      int16_t share;
      if ((FLEET(FP_SEG(followed), FP_OFF(followed))->flags & FLEET_FLAG_HANDLED_maybe) != 0) {
        share = (int16_t)fleet_step_left(fseg, foff);
      } else {
        share = (int16_t)((int16_t)(fleet_step_left(fseg, foff) + fleet_step_done(fseg, foff) + 4) / 5);
        if (share > (int16_t)fleet_step_left(fseg, foff)) share = (int16_t)fleet_step_left(fseg, foff);
      }
      move_left = share;
      fuel_cap -= (int16_t)fleet_step_done(fseg, foff);
      if ((int16_t)((uint32_t)fuel_cap >> 16) < 0) fuel_cap = 0;
    }
    const double distance = points_distance(start, target);
    {
      const int32_t whole = (int32_t)(int16_t)(uint16_t)fp80_to_i32_ftol(fp80_add(fp80_from_f64(ds_double(0x1fb8)), fp80_from_f64(distance)));
      if (whole <= move_left) move_left = whole;
    }
    int32_t fuel_used;
    if (move_left > fuel_cap) { /* not enough fuel for the whole move */
      FLEET(fseg, foff)->fuel = 0;
      fuel_used = 1;
      move_left = fuel_cap;
    } else {
      if ((int16_t)pass > 0) {
        FLEET(fseg, foff)->fuel += fleet_fuel_used_maybe(fseg, foff);
        move_left += (int16_t)fleet_step_done(fseg, foff);
      }
      fuel_used = (int32_t)fleet_fuel_figure(fleet, 0xffff, (uint32_t)move_left, 0);
      if ((int16_t)pass > 0) {
        set_fleet_fuel_used_maybe(fseg, foff, (uint32_t)fuel_used);
        move_left -= (int16_t)fleet_step_done(fseg, foff);
      }
      FLEET(fseg, foff)->fuel = (uint32_t)(FLEET(fseg, foff)->fuel - (uint32_t)fuel_used);
      if ((int16_t)(FLEET(fseg, foff)->fuel >> 16) < 0) FLEET(fseg, foff)->fuel = 0;
    }
    if (FLEET(fseg, foff)->fuel == 0 && fuel_used > 0) {
      /* out of fuel: unless it is still before the end of the waypoint and has some range, slow down to the warp that needs none */
      uint32_t c0, c2, c3;
      fp_cmp(fp80_sub(fp80_from_i32(move_left), fp80_from_f64(ds_double(0x1fc0))), fp80_from_f64(distance), &c0, &c2, &c3);
      const bool beyond = !(c0 != 0 || c3 != 0);
      if (!(beyond && fuel_cap != 0) && !enough_fuel) {
        uint16_t warp = 0;
        do {
          warp++;
        } while (fleet_fuel_figure(fleet, warp, 0xffffffffu, 0) == 0 && (int16_t)warp < 10);
        if ((int16_t)warp > 1) {
          wp1->flags = (uint16_t)(wp1->flags ^ ((wp1->flags ^ (uint16_t)((warp - 1) << 4)) & 0xf0));
          fleet_message2(owner, MSG_OUT_OF_FUEL_SLOWED_maybe, fleet_id, fleet_id, (uint16_t)(warp - 1));
        } else {
          fleet_message2(owner, MSG_OUT_OF_FUEL_STOPPED_maybe, fleet_id, fleet_id, 0);
        }
        out_of_fuel = true;
      }
    }
    if (fuel_cap == 0) return;
    FLEET(fseg, foff)->flags = (uint8_t)(FLEET(fseg, foff)->flags & 0xdf);
    int16_t dx = (int16_t)((uint16_t)target - (uint16_t)start), dy = (int16_t)((uint16_t)(target >> 16) - (uint16_t)(start >> 16));
    if (dx != 0 || dy != 0) { /* the heading of the move */
      set_fleet_move_state(fseg, foff, (uint8_t)(fleet_move_state(fseg, foff) | 0x10));
      for (;;) {
        const int16_t adx = (int16_t)(dx < 0 ? -dx : dx), ady = (int16_t)(dy < 0 ? -dy : dy);
        if (adx <= 0x7f && ady <= 0x7f) break;
        dx = (int16_t)(dx / 2);
        dy = (int16_t)(dy / 2);
      }
      wr16(fseg, (uint16_t)(foff + 0x74), (uint16_t)(((uint8_t)(dy + 0x7f) << 8) | (uint8_t)(dx + 0x7f)));
      set_fleet_move_state(fseg, foff, (uint8_t)((fleet_move_state(fseg, foff) & 0xf0) | ((wp1->flags >> 4) & 0xf)));
    }
    const int32_t reach = fp80_to_i32_ftol(fp80_sub(fp80_from_f64(distance), fp80_from_f64(ds_double(0x1fc8)))); /* the distance, less one */
    RANGE_OUT_WORD = reach <= move_left ? (uint16_t)reach : (uint16_t)move_left;
    bool skip_scoop = false;
    if ((wp1->flags & 0xf0) < 0xb0) {
      const uint16_t a[5] = {foff, fseg, (uint16_t)(rt_bp - 0x2a), 0, 0};
      if ((uint16_t)far_call(22, 0x30b6, a, 5) == 0) { /* hit by a minefield */
        set_fleet_step_left(fseg, foff, 0);
        if ((FLEET(fseg, foff)->flags & FLEET_FLAG_EMPTY) != 0) return;
        if ((int32_t)(int16_t)RANGE_OUT_WORD < reach) move_left = (int32_t)(int16_t)RANGE_OUT_WORD;
        skip_scoop = true;
      }
    }
    if (!skip_scoop && !out_of_fuel) {
      /* a ram scoop makes fuel on the way */
      if ((int32_t)CALL_ORIG(10, 0x3ac6, FAR_CDECL, 2, foff, fseg) > 0) {
        const int32_t length = reach > move_left ? move_left : reach;
        const uint32_t made = (uint32_t)CALL_ORIG(7, 0x3726, FAR_CDECL, 5, foff, fseg, waypoint_warp(wp1), (uint16_t)length, (uint16_t)((uint32_t)length >> 16));
        if ((int32_t)made > 0) {
          (void)CALL_ORIG(10, 0x3aec, FAR_CDECL, 6, 2, fleet_id, 4, (uint16_t)made, (uint16_t)(made >> 16), 0);
          const uint16_t shown = (int32_t)made > 0x7ef4 ? 0x7ef4 : (uint16_t)made;
          fleet_message2(owner, MSG_FUEL_USE_maybe, fleet_id, fleet_id, shown);
        }
      }
    }
    arrives = !(reach >= move_left && reach > 0);
    if (!arrives) { /* a partial move along the course */
      const double bias_x = ds_double((int16_t)(uint16_t)target <= (int16_t)(uint16_t)start ? 0x1fa0 : 0x1f98);
      const double bias_y = ds_double((int16_t)(uint16_t)(target >> 16) <= (int16_t)(uint16_t)(start >> 16) ? 0x1fa0 : 0x1f98);
      if (ds_double(0x1fa8) < distance || !(ds_double(0x1fb0) <= distance)) {
        const double ratio = fp80_to_f64(fp80_div(fp80_from_i32(move_left), fp80_from_f64(distance)));
        const int16_t move_x = (int16_t)((uint16_t)target - (uint16_t)start), move_y = (int16_t)((uint16_t)(target >> 16) - (uint16_t)(start >> 16));
        FLEET(fseg, foff)->x = (uint16_t)((uint16_t)fp80_to_i32_ftol(fp80_add(fp80_mul(fp80_from_i16(move_x), fp80_from_f64(ratio)), fp80_from_f64(bias_x))) + (uint16_t)start);
        FLEET(fseg, foff)->y = (uint16_t)((uint16_t)fp80_to_i32_ftol(fp80_add(fp80_mul(fp80_from_i16(move_y), fp80_from_f64(ratio)), fp80_from_f64(bias_y))) + (uint16_t)(start >> 16));
        FLEET(fseg, foff)->planet_id = 0xffff;
      }
      if ((int16_t)pass > 0 && (int16_t)fleet_step_left(fseg, foff) > 0) {
        set_fleet_step_done(fseg, foff, (uint16_t)(fleet_step_done(fseg, foff) + (uint16_t)move_left));
        set_fleet_step_left(fseg, foff, (uint16_t)(fleet_step_left(fseg, foff) - (uint16_t)move_left));
        if ((int16_t)fleet_step_left(fseg, foff) > 0 && !out_of_fuel) {
          *all_done = false;
          FLEET(fseg, foff)->flags = (uint8_t)(FLEET(fseg, foff)->flags & 0xf7);
        }
      }
    }
  }
  if (arrives) { /* at the waypoint */
    FLEET(fseg, foff)->xy = target;
    FLEET(fseg, foff)->planet_id = waypoint_target_kind(wp1) == 1 ? wp1->target_id : 0xffff;
    if ((int16_t)pass > 0) {
      const uint32_t followed = fleet_followed_maybe(fseg, foff);
      FLEET(FP_SEG(followed), FP_OFF(followed))->flags |= FLEET_FLAG_HANDLED_maybe;
    }
  }
  /* colonists die on long trips */
  if ((g_ui_flags_a_maybe & 0x40) != 0 && (int32_t)fleet_colonists(fseg, foff) > 0 && (int16_t)pass <= 1) colonists_die_travelling(fseg, foff);
  /* a fleet that gets to the mouth of a wormhole comes out at its other end */
  if (FLEET(fseg, foff)->xy == (wp1->x | ((uint32_t)wp1->y << 16)) && waypoint_target_kind(wp1) == 8) {
    const uint32_t hole = map_object_by_id(wp1->target_id);
    if (hole != 0 && map_object_kind(FAR_REC(struct MapObject, FP_SEG(hole), FP_OFF(hole))->owner_kind_bits) == 0x40) {
      const uint16_t seen = (uint16_t)(1u << (owner & 0x1f));
      const uint32_t other = map_object_by_id(object_word_at(FP_SEG(hole), FP_OFF(hole), 0xc));
      {
        const uint16_t a[2] = {foff, fseg};
        (void)far_call(16, 0x133e, a, 2);
      }
      set_object_word_at(FP_SEG(hole), FP_OFF(hole), 0xa, (uint16_t)(object_word_at(FP_SEG(hole), FP_OFF(hole), 0xa) | seen));
      set_object_word_at(FP_SEG(other), FP_OFF(other), 0xa, (uint16_t)(object_word_at(FP_SEG(other), FP_OFF(other), 0xa) | seen));
      set_object_word_at(FP_SEG(other), FP_OFF(other), 8, (uint16_t)(object_word_at(FP_SEG(other), FP_OFF(other), 8) | seen));
      FLEET(fseg, foff)->xy = FAR_REC(struct MapObject, FP_SEG(other), FP_OFF(other))->xy;
      wp1->x = (uint16_t)FAR_REC(struct MapObject, FP_SEG(other), FP_OFF(other))->xy;
      wp1->y = (uint16_t)(FAR_REC(struct MapObject, FP_SEG(other), FP_OFF(other))->xy >> 16);
    }
  }
  /* a fleet in deep space may be at a planet */
  if (FLEET(fseg, foff)->planet_id == 0xffff) {
    const uint16_t a[4] = {(uint16_t)FLEET(fseg, foff)->xy, (uint16_t)(FLEET(fseg, foff)->xy >> 16), 0x81, (uint16_t)(rt_bp - 0x14)};
    if ((uint16_t)far_call(7, 0x294e, a, 4) != 0) FLEET(fseg, foff)->planet_id = PLANET_SEARCH_PLANET;
  }
  /* waypoint 0 is now the fleet's place */
  wp0->x = (uint16_t)FLEET(fseg, foff)->xy;
  wp0->y = (uint16_t)(FLEET(fseg, foff)->xy >> 16);
  wp0->target_id = FLEET(fseg, foff)->planet_id;
  wp0->flags = (uint16_t)((wp0->flags & 0xf0ff) | ((FLEET(fseg, foff)->planet_id == 0xffff ? 4 : 1) << 8));
  if (ENOUGH_FUEL_KEPT == 0) return;
  /* a fleet that has less fuel than the trip needs is refuelled up to that (at most its tanks) */
  const int32_t needed = (int32_t)fleet_fuel_figure(fleet, 0xffff, 0xffffffffu, 0);
  if ((int32_t)FLEET(fseg, foff)->fuel >= needed) return;
  const int32_t tank = (int32_t)fleet_max_fuel(fleet);
  FLEET(fseg, foff)->fuel = tank > needed ? (uint32_t)needed : (uint32_t)fleet_max_fuel(fleet);
}

/* move_fleets: move every fleet along its course for the turn.
 * 22:0x1f18 FUN_10a8_1f18, FAR CDECL (no argument), 4510 bytes (ENTER 0x16c,0; PUSH DI; PUSH SI), x87.
 * Walks the fleet pointer array in passes (a fleet that has more distance to cover than one step is revisited, up to 11 passes).
 * A fleet with at least two waypoints whose second waypoint has a warp speed gets: the engine-failure and stargate
 * checks (10:0x6312 fuel use, 7:0x0af8 / 7:0x4bde stargate lookups, rolls of 8:0x8730), the distance it can cover at
 * its warp (squares of the warp, ftol 34:0x0de6, sqrt 34:0x0d68), the new position written to the fleet (x, y at +8,
 * +0xA), its fuel at +0x5C reduced, the mines on the way (22:0x30b6 fleet_hits_minefield), the arrival at the
 * waypoint, and the messages 6:0x7344 / 6:0x7328 (ids 0xf2, 0x102, 0xc1, ...). At the end the waypoint lists are updated (16:0x1060). */
DECOMP(22, 0x1f18, move_fleets, FAR_CDECL)
void move_fleets(void)
{
  FRAME_ENTER_REGS(0x16c, 2);
  uint16_t pass = 0;
  if ((int16_t)g_fleet_count > 0) {
    for (;;) {
      bool all_done = true;
      for (uint16_t index = 0; (int16_t)index < (int16_t)g_fleet_count; index++) {
        const uint32_t fleet = fleet_ptr_at((uint16_t)(index << 2));
        if (fleet == 0) break;
        move_one_fleet(fleet, pass, &all_done);
      }
      if (all_done) break;
      if (pass++ >= 10) break;
    }
    (void)far_call(16, 0x1060, NULL, 0);
  }
  FRAME_LEAVE();
}
#undef RANGE_OUT_WORD
#undef PLANET_SEARCH_OUT
#undef PLANET_SEARCH_PLANET
#undef FLEET_COPY2
#undef FLEET_LOST2
/* The data 22:0x30b6 reads through CS (22:0x0e6e to 0x0e91, 18 words), by mine field type (0, 1, 2). The hit chance and
 * the safe warp match the known Stars! mine fields (standard 0.3 percent and warp 4, heavy 1 percent and warp 6, speed
 * bump 3.5 percent and warp 5). A type outside 0 to 2 (a damaged world) reads on into the code segment, as the original. */
static inline int16_t cs_word22(uint16_t off) { return (int16_t)rd16(SEG_SEL(22), off); }
static inline int16_t mine_hit_chance_per_thousand(uint16_t type) { return cs_word22((uint16_t)(0x0e6e + 2 * type)); }
/* The two damage words of a type (index `row` 0, or 1 when a design has the engine flag): 22:0x0e74 per fleet, 22:0x0e80 per ship. */
static inline int16_t mine_fleet_damage_maybe(uint16_t type, uint16_t row) { return cs_word22((uint16_t)(0x0e74 + 2 * (2 * type + row))); }
static inline int16_t mine_ship_damage_maybe(uint16_t type, uint16_t row) { return cs_word22((uint16_t)(0x0e80 + 2 * (2 * type + row))); }
static inline int16_t mine_safe_warp(uint16_t type) { return cs_word22((uint16_t)(0x0e8c + 2 * type)); }

enum { MINE_TYPES = 3, MAX_CROSSINGS = 8 };

/* The parts of the course (distance along the segment, in whole units) that lie inside the enemy fields, per field
 * type: sorted, merged intervals [lo, hi]. */
struct MineCrossings {
  uint16_t count[MINE_TYPES];
  int16_t lo[MINE_TYPES][MAX_CROSSINGS];
  int16_t hi[MINE_TYPES][MAX_CROSSINGS];
};

/* Adds the interval [t_in, t_out] to the list of `type`, keeping it sorted and merging the intervals that touch it
 * (a gap of one unit still counts as touching); a ninth interval is dropped. */
static void mine_crossing_add(struct MineCrossings *c, uint16_t type, int16_t t_in, int16_t t_out)
{
  int16_t *lo = c->lo[type], *hi = c->hi[type];
  uint16_t at = 0;
  /* the first interval that ends at or after the start of the new one */
  if ((int16_t)c->count[type] > 0) {
    do {
      if (t_in <= hi[at]) break;
      at++;
    } while ((int16_t)c->count[type] > (int16_t)at);
  }
  if (c->count[type] == at) { /* behind all of them */
    if ((int16_t)at >= MAX_CROSSINGS) return;
    lo[at] = t_in;
    hi[at] = t_out;
    c->count[type]++;
    return;
  }
  if ((int16_t)(uint16_t)(lo[at] - 1) > t_out) { /* in front of interval `at`, not touching it */
    if ((int16_t)c->count[type] >= MAX_CROSSINGS) return;
    for (uint16_t i = c->count[type]; i > at; i--) {
      lo[i] = lo[i - 1];
      hi[i] = hi[i - 1];
    }
    lo[at] = t_in;
    hi[at] = t_out;
    c->count[type]++;
    return;
  }
  /* it touches interval `at`: widen that one, then swallow the following intervals that start inside it */
  if (lo[at] > t_in) lo[at] = t_in;
  if (hi[at] >= t_out) return;
  hi[at] = t_out;
  uint16_t next = (uint16_t)(at + 1);
  if ((int16_t)c->count[type] > (int16_t)next) {
    do {
      if (t_out < lo[next]) break;
      next++;
    } while ((int16_t)c->count[type] > (int16_t)next);
  }
  if (hi[next - 1] > t_out) hi[at] = hi[next - 1];
  for (uint16_t i = next; i < c->count[type]; i++) {
    lo[at + 1 + (i - next)] = lo[i];
    hi[at + 1 + (i - next)] = hi[i];
  }
  c->count[type] = (uint16_t)(c->count[type] + (uint16_t)((at + 1) - next));
}

/* The squared radius of a minefield: the dword at +6 of its map object (struct MapObject has no field for it). */
static inline uint32_t minefield_radius_sq(uint16_t seg, uint16_t off) { return rd32(seg, (uint16_t)(off + 6)); }
static inline void set_minefield_radius_sq(uint16_t seg, uint16_t off, uint32_t v) { wr32(seg, (uint16_t)(off + 6), v); }
/* The mine type (0 to 2) of a minefield: the byte at +0xc of its map object (MapObject.fld_0c). */
static inline uint16_t minefield_type(uint16_t seg, uint16_t off) { return FAR_REC(struct MapObject, seg, off)->fld_0c; }
/* The two words of a minefield that name the players who saw it: MapObject.seen_by_mask at +0xe and the word at +0xa. */
static inline void minefield_seen_by(uint16_t seg, uint16_t off, uint16_t bit)
{
  FAR_REC(struct MapObject, seg, off)->seen_by_mask |= bit;
  wr16(seg, (uint16_t)(off + 0xa), (uint16_t)(rd16(seg, (uint16_t)(off + 0xa)) | bit));
}
/* The engine record (2:0x50a2) of an engine item has a zero word at +0x3e for the engines the engine flag is about. */
static inline bool engine_record_has_zero_flag(uint16_t item)
{
  const uint32_t record = far_call(2, 0x50a2, &item, 1);
  return rd16(FP_SEG(record), (uint16_t)(FP_OFF(record) + 0x3e)) == 0;
}

/* The message ids 22:0x30b6 sends with 6:0x7344. Each case sends the fleet's owner a message (when the field is not his own) and
 * the field's owner another; ids from 0x160 on are the ones for a field that was handed in (a hit by a field already known).
 * The wording is in the game's string table, which was not read here: the names say what the branch means. */
enum {
  MSG_FLEET_HIT_FIELD_UNHURT_maybe = 0xc5,         /* damage 0 */
  MSG_FIELD_HIT_BY_FLEET_UNHURT_maybe = 0xc9,
  MSG_FLEET_DAMAGED_maybe = 0xc6,                  /* damage, no ship lost */
  MSG_FLEET_DAMAGED_GIVEN_FIELD_maybe = 0x160,
  MSG_FIELD_DAMAGED_FLEET_maybe = 0xca,
  MSG_FIELD_DAMAGED_FLEET_GIVEN_maybe = 0x163,
  MSG_FLEET_LOST_SHIPS_maybe = 0xc7,               /* some ships lost */
  MSG_FLEET_LOST_SHIPS_GIVEN_maybe = 0x161,
  MSG_FIELD_KILLED_SHIPS_maybe = 0xcb,
  MSG_FIELD_KILLED_SHIPS_GIVEN_maybe = 0x164,
  MSG_FLEET_DESTROYED_SALVAGE_maybe = 0xc8,        /* every ship lost, with salvage */
  MSG_FIELD_DESTROYED_FLEET_maybe = 0xcc,
  MSG_FLEET_DESTROYED_maybe = 0x15f,               /* every ship lost, no salvage */
  MSG_FLEET_DESTROYED_OWN_FIELD_maybe = 0x162,
  MSG_SUBJECT_SALVAGE = 0xfffa,
  MSG_SUBJECT_NONE = 0xffff
};

/* Stack slots of 22:0x30b6 whose address a callee gets. */
#define ENTRY_DIST L16(-0x32)                       /* 8:0xae30 out: where the course enters a field */
#define EXIT_DIST L16(-0x30)                        /* 8:0xae30 out: where it leaves it */
#define HIT_XY L32(-0x3a)                           /* where the fleet was hit (x at -0x3a, y at -0x38); 30:0x183a gets its address */
#define HIT_X L16(-0x3a)
#define HIT_Y L16(-0x38)
#define SALVAGE L32(-0x46)                          /* far pointer to the salvage object; 30:0x183a gets its address */
#define FLEET_COPY BP_LOCAL(struct Fleet, -0x150)   /* the fleet being damaged; 10:0x6c1c gets its address */
#define FLEET_LOSSES BP_LOCAL(struct Fleet, -0x1cc) /* a fleet record that holds the ships it loses */
/* fleet_hits_minefield: does a fleet moving along its course run into an enemy minefield, and what is left of it.
 * 22:0x30b6 FUN_10a8_30b6, FAR CDECL (fleet [BP+6] far pointer, range_ptr [BP+0xA] a near pointer to the word with
 * the distance the fleet moves, minefield [BP+0xC] a far pointer to the minefield that was hit or 0), AX = 1 or 0,
 * 3599 bytes (ENTER 0x22e,0; PUSH DI; PUSH SI), x87, GDI.MulDiv.
 * With minefield 0 it looks for the minefields (map objects of kind 0, another owner, not at peace with the fleet's
 * owner: relations byte not 1) that the segment from the fleet's position to its second waypoint crosses
 * (8:0xae30 gives the entry and exit distance per field), keeps the sorted, merged crossings per mine type, and walks
 * them in order of distance: a crossing whose type is not safe at the fleet's warp (the warp that the move needs,
 * 3 to 10, against the type's safe warp plus 1 for race 1 or 2 for race 5) is rolled with 8:0x8730(1000) once per
 * distance unit against (warp excess * hit chance); the fleet stops at the first unit that is hit. With a minefield
 * given the hit is that field. The fleet's ships are then damaged or destroyed design by design (the damage per ship
 * from the mine type, the number of ships and the design's strength; 10:0x6c1c removes the lost ships); the lost
 * ships' cargo goes into a salvage object (30:0x183a); the mine owner may learn the designs; messages follow
 * (6:0x7344); the field that was hit shrinks or disappears (33:0x015a), and the fleet is flagged (bit 6). AX = 0
 * when the fleet was hit (or the given field did no damage), 1 when it got through. */
DECOMP(22, 0x30b6, fleet_hits_minefield, FAR_CDECL)
uint16_t fleet_hits_minefield(uint32_t fleet, uint16_t range_ptr, uint32_t minefield)
{
  FRAME_ENTER_REGS(0x22e, 2);
  const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
  const uint16_t owner = FLEET(fseg, foff)->owner;
  const uint16_t range = NEAR_AT(uint16_t, range_ptr); /* the distance the fleet moves */
  uint16_t warp = 0;                                   /* the warp that move needs (course only) */
  uint16_t victim_designs[FLEET_DESIGN_SLOTS];         /* the design slots that suffer, in order */
  uint16_t victims = 0;
  int32_t total_ships = 0;
  bool engine_flag_maybe = false; /* a design's engine record has a zero word at +0x3e: it raises the damage per ship */
  uint16_t type = 0;              /* the mine type that is hit */
  uint16_t killed = 0;            /* ships destroyed */
  uint32_t total_damage = 0;
  int16_t hit_distance = 0;       /* distance along the course up to the hit */
  uint16_t speed_bonus;           /* the race raises the speed a fleet can take through a field */
  uint16_t start_x = 0, start_y = 0, end_x = 0, end_y = 0;

  SALVAGE = 0;
  {
    const uint16_t race = player_race(owner);
    speed_bonus = (uint16_t)((race == 5 ? 2 : 0) + (race == 1 ? 1 : 0));
  }

  struct MineCrossings crossings;
  memset(&crossings, 0, sizeof crossings);
  if (minefield == 0) {
    start_x = FLEET(fseg, foff)->x;
    start_y = FLEET(fseg, foff)->y;
    {
      const uint32_t list = FLEET(fseg, foff)->waypoints;
      const struct Waypoint *second =
          FAR_REC(struct Waypoint, FP_SEG(list), (uint16_t)(FP_OFF(list) + sizeof(struct WaypointListHdr) + sizeof(struct Waypoint)));
      end_x = second->x;
      end_y = second->y;
    }
    /* the smallest warp 3 to 10 with warp * warp >= distance - 1 */
    warp = 3;
    do {
      if ((int16_t)(warp * warp) >= (int16_t)(uint16_t)(range - 1)) break;
      warp++;
    } while ((int16_t)warp < 10);
    if ((int16_t)(uint16_t)(speed_bonus + 3) >= (int16_t)warp ||
        (((uint32_t)end_y << 16) | end_x) == (((uint32_t)start_y << 16) | start_x)) {
      FRAME_LEAVE();
      return 1; /* no field is dangerous at that speed, or the fleet does not move */
    }
    /* the crossings of the course with the enemy fields */
    const uint16_t first_object = g_map_objects, object_end = map_objects_end(), object_seg = g_map_objects_segment_maybe;
    for (uint16_t cur = first_object; cur < object_end; cur = (uint16_t)(cur + sizeof(struct MapObject))) {
      const uint8_t bits = FAR_REC(struct MapObject, object_seg, cur)->owner_kind_bits;
      const uint16_t obj_owner = map_object_owner(bits);
      if (obj_owner == owner) continue;
      if (map_object_kind(bits) != MAPOBJ_KIND_MINEFIELD) continue;
      if (player_relation(obj_owner, owner) == 1) continue;
      const uint32_t radius_sq = minefield_radius_sq(object_seg, cur);
      const uint32_t center = FAR_REC(struct MapObject, object_seg, cur)->xy;
      if ((uint16_t)CALL_ORIG(8, 0xae30, FAR_CDECL, 11, start_x, start_y, end_x, end_y, (uint16_t)center, (uint16_t)(center >> 16),
                              (uint16_t)radius_sq, (uint16_t)(radius_sq >> 16), range, (uint16_t)(rt_bp - 0x32),
                              (uint16_t)(rt_bp - 0x30)) == 0)
        continue;
      mine_crossing_add(&crossings, minefield_type(object_seg, cur), (int16_t)ENTRY_DIST, (int16_t)EXIT_DIST);
    }
    if ((uint16_t)(crossings.count[0] + crossings.count[1] + crossings.count[2]) == 0) {
      FRAME_LEAVE();
      return 1; /* the course crosses no field */
    }
  }

  /* the fleet's ships: how many, and the engine flag */
  {
    const uint32_t designs = player_designs(owner);
    for (uint16_t slot = 0; slot < FLEET_DESIGN_SLOTS; slot++) {
      const int16_t count = (int16_t)FLEET(fseg, foff)->ship_count[slot];
      if (count <= 0) continue;
      total_ships += count;
      if (engine_record_has_zero_flag(design_of(designs, slot)->slots[0].item)) engine_flag_maybe = true;
    }
  }

  if (minefield == 0) {
    /* the first crossing that is hit, in order of distance */
    uint16_t used[MINE_TYPES] = {0, 0, 0};
    int16_t remaining = (int16_t)(crossings.count[0] + crossings.count[1] + crossings.count[2]);
    for (;;) {
      int16_t start = 10000;
      uint16_t next_type = 0xffff;
      for (uint16_t t = 0; t < MINE_TYPES; t++) {
        if ((int16_t)used[t] >= (int16_t)crossings.count[t]) continue;
        if (crossings.lo[t][used[t]] < start) {
          start = crossings.lo[t][used[t]];
          next_type = t;
        }
      }
      type = next_type;
      const int16_t length = (int16_t)(uint16_t)(crossings.hi[type][used[type]] - crossings.lo[type][used[type]]);
      const uint16_t safe_warp = (uint16_t)(mine_safe_warp(type) + speed_bonus);
      if ((int16_t)safe_warp < (int16_t)warp) { /* not safe at this warp: roll for every distance unit */
        const int16_t chance = (int16_t)(uint16_t)((uint16_t)(warp - safe_warp) * (uint16_t)mine_hit_chance_per_thousand(type));
        int16_t unit = 0;
        if (length > 0) {
          do {
            if ((int16_t)rand_below(1000) < chance) break;
            unit++;
          } while (unit < length);
        }
        if (unit != length) { /* hit */
          hit_distance = (int16_t)(unit + start);
          break;
        }
      }
      used[type]++; /* no hit in this crossing */
      remaining--;
      if (remaining <= 0) {
        FRAME_LEAVE();
        return 1;
      }
    }
  } else {
    type = minefield_type(FP_SEG(minefield), FP_OFF(minefield));
  }

  /* the damage */
  const uint16_t damage_row = engine_flag_maybe ? 1 : 0;
  const int16_t ship_damage = mine_ship_damage_maybe(type, damage_row);
  if (ship_damage != 0) {
    const uint32_t designs = player_designs(owner);
    int16_t base_damage = 0; /* spent on the first design only */
    {
      const uint16_t left = (uint16_t)(mine_fleet_damage_maybe(type, damage_row) - (uint16_t)(ship_damage * (int16_t)total_ships));
      if (total_ships < 5 && (int16_t)left > 0) base_damage = (int16_t)left;
    }
    memcpy(&FLEET_COPY, FLEET(fseg, foff), sizeof(struct Fleet));
    memset(&FLEET_LOSSES, 0, sizeof(struct Fleet));
    FLEET_LOSSES.owner = FLEET_COPY.owner;
    FLEET_LOSSES.flags |= FLEET_FLAG_EMPTY;
    FLEET_LOSSES.version = 7;
    for (uint16_t slot = 0; slot < FLEET_DESIGN_SLOTS; slot++) {
      const int16_t count = (int16_t)FLEET(fseg, foff)->ship_count[slot];
      if (count <= 0) continue;
      if (minefield != 0 &&
          map_object_owner(FAR_REC(struct MapObject, FP_SEG(minefield), FP_OFF(minefield))->owner_kind_bits) == owner) {
        const uint16_t hull = design_of(designs, slot)->hull;
        if (hull == 0x1b || hull == 0x1c) continue; /* the mine layers of the field's own owner are safe */
      }
      victim_designs[victims++] = slot;
      const int32_t ships = count;
      const int32_t engines = design_of(designs, slot)->slots[0].count;
      const int32_t strength = design_of(designs, slot)->strength;
      const uint16_t design_off = (uint16_t)(FP_OFF(designs) + (uint16_t)(slot * sizeof(struct ShipDesign)));
      /* the defence of the design (7:0x0a0e) times the number of ships */
      const int32_t defense =
          (int32_t)((uint32_t)CALL_ORIG(7, 0x0a0e, FAR_CDECL, 3, design_off, FP_SEG(designs), owner) * (uint32_t)ships);
      /* the damage of this design: engines * (ships * damage per ship + the base damage, once) */
      const int32_t design_damage =
          (int32_t)(((uint32_t)ships * (uint32_t)(int32_t)ship_damage + (uint32_t)(int32_t)base_damage) * (uint32_t)engines);
      total_damage += (uint32_t)design_damage;
      /* what the ships have already: percent of the ships * strength * damaged ships / 500 */
      const uint16_t old_damage = FLEET(fseg, foff)->damage[slot];
      const int32_t damaged_ships = old_damage >> FLEET_DAMAGE_SHIFT;
      const int32_t damaged_now = ((int32_t)(old_damage & FLEET_DAMAGE_PCT_MASK) * (int32_t)count) / 100;
      const int32_t already = (int32_t)((uint32_t)damaged_now * (uint32_t)strength * (uint32_t)damaged_ships) / 500;
      const int32_t half = design_damage >> 1;
      const int32_t absorbed = half <= defense ? half : defense;
      const int32_t effective = (int32_t)((uint32_t)already - (uint32_t)absorbed + (uint32_t)design_damage);
      base_damage = 0;
      const int32_t per_ship = effective / ships;
      if (strength < per_ship) { /* more damage per ship than the design can take: all of them are lost */
        killed = (uint16_t)(killed + (uint16_t)count);
        FLEET_LOSSES.ship_count[slot] = (uint16_t)count;
        FLEET_COPY.ship_count[slot] = 0;
      } else { /* the ships stay, all damaged: the damage per ship in units of strength / 500 */
        const uint16_t damage_field =
            (uint16_t)((uint16_t)((int32_t)((uint32_t)per_ship * 500u) / strength) << FLEET_DAMAGE_SHIFT);
        FLEET_COPY.damage[slot] = damage_field == 0 ? (uint16_t)(100 | (1 << FLEET_DAMAGE_SHIFT)) : (uint16_t)(damage_field | 100);
      }
    }
    if (minefield != 0 && total_damage == 0) {
      FRAME_LEAVE();
      return 0;
    }
    if ((int32_t)(int16_t)killed != total_ships)
      (void)CALL_ORIG(10, 0x6c1c, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x150), (uint16_t)(rt_bp - 0x1cc));
    memcpy(FLEET(fseg, foff), &FLEET_COPY, sizeof(struct Fleet));
    if ((int32_t)(int16_t)killed == total_ships) FLEET(fseg, foff)->flags |= FLEET_FLAG_EMPTY;
  }

  /* the field that did it */
  uint16_t mine_seg, mine_off;
  int32_t shrink = 0;
  if (minefield == 0) {
    {
      const int16_t dx = (int16_t)(end_x - start_x), dy = (int16_t)(end_y - start_y);
      const int32_t length_sq = (int32_t)((uint32_t)((int32_t)dy * (int32_t)dy) + (uint32_t)((int32_t)dx * (int32_t)dx));
      /* x87: sqrt of the squared length (the C runtime's, 34:0x0d68) plus the constant at DS:0x1f98, then the C
       * runtime's conversion to an integer (34:0x0de6) */
      Fp80 length_fp = crt_sqrt_34_0d68(fp80_from_i32(length_sq));
      length_fp = fp80_add(length_fp, fp80_from_f64(rdf64(MEM_SEL_DGROUP, 0x1f98)));
      const int16_t length = (int16_t)(uint16_t)fp80_to_i32_ftol(length_fp);
      HIT_X = (uint16_t)(MulDiv(dx, hit_distance, length) + (int16_t)start_x);
      HIT_Y = (uint16_t)(MulDiv(dy, hit_distance, length) + (int16_t)start_y);
    }
    if (killed != 0) { /* the lost ships' cargo: into the salvage object at that very place, or a new one */
      const uint16_t first_object = g_map_objects, object_end = map_objects_end(), object_seg = g_map_objects_segment_maybe;
      uint16_t cur = first_object;
      for (; cur < object_end; cur = (uint16_t)(cur + sizeof(struct MapObject))) {
        const struct MapObject *obj = FAR_REC(struct MapObject, object_seg, cur);
        if (obj->xy == HIT_XY && map_object_kind(obj->owner_kind_bits) == MAPOBJ_KIND_PACKET &&
            (map_object_byte7(object_seg, cur) & MAPOBJ_PACKET_WARP_MASK) == 0)
          break;
      }
      SALVAGE = cur == object_end ? 0 : (((uint32_t)object_seg << 16) | cur);
      const uint16_t id_owner = (uint16_t)((FLEET_COPY.id >> 9) & 0xf);
      (void)CALL_ORIG(30, 0x183a, FAR_CDECL, 5, (uint16_t)(rt_bp - 0x46), (uint16_t)(foff + FLD(struct Fleet, cargo)), fseg, id_owner,
                      (uint16_t)(rt_bp - 0x3a));
    }
    /* the field that did it: of the hit type, the one whose edge is nearest to the hit */
    int32_t nearest = 100000000;
    /* if no field qualifies the original goes on with stale registers: the owner's speed bonus as the offset and the
     * move distance as the segment; the hit field is always among the objects, so that does not happen */
    mine_off = speed_bonus;
    mine_seg = range;
    const uint16_t first_object = g_map_objects, object_end = map_objects_end(), object_seg = g_map_objects_segment_maybe;
    for (uint16_t cur = first_object; cur < object_end; cur = (uint16_t)(cur + sizeof(struct MapObject))) {
      const struct MapObject *obj = FAR_REC(struct MapObject, object_seg, cur);
      const uint8_t bits = obj->owner_kind_bits;
      const uint16_t obj_owner = map_object_owner(bits);
      if (obj_owner == owner) continue;
      if (map_object_kind(bits) != MAPOBJ_KIND_MINEFIELD) continue;
      if (player_relation(obj_owner, owner) == 1) continue;
      if (minefield_type(object_seg, cur) != type) continue;
      const int16_t dx = (int16_t)((uint16_t)obj->xy - HIT_X), dy = (int16_t)((uint16_t)(obj->xy >> 16) - HIT_Y);
      const int32_t edge = (int32_t)((uint32_t)((int32_t)dx * (int32_t)dx) + (uint32_t)((int32_t)dy * (int32_t)dy) -
                                     minefield_radius_sq(object_seg, cur));
      if (edge < nearest) {
        nearest = edge;
        mine_off = cur;
        mine_seg = object_seg;
      }
    }
    /* the field shrinks by a twentieth of its squared radius, at least 10 and, above 50, a hundredth but at least 50 */
    const int32_t radius_sq = (int32_t)minefield_radius_sq(mine_seg, mine_off);
    shrink = radius_sq / 20;
    if (shrink > 0x32) {
      shrink = radius_sq / 100;
      if (shrink < 0x32) shrink = 0x32;
    } else if (shrink < 10) {
      shrink = 10;
    }
  } else {
    mine_seg = FP_SEG(minefield);
    mine_off = FP_OFF(minefield);
    HIT_XY = FAR_REC(struct MapObject, mine_seg, mine_off)->xy;
  }

  const uint16_t mine_owner = map_object_owner(FAR_REC(struct MapObject, mine_seg, mine_off)->owner_kind_bits);
  /* a mine owner of race 5 learns the designs that hit the field */
  if (player_race(mine_owner) == 5) {
    const uint32_t designs = player_designs(owner);
    const uint16_t seen = (uint16_t)(1u << (FAR_REC(struct MapObject, mine_seg, mine_off)->id_kind >> 9 & 0xf));
    if (victims == 0) {
      for (uint16_t slot = 0; slot < FLEET_DESIGN_SLOTS; slot++)
        if ((int16_t)FLEET(fseg, foff)->ship_count[slot] > 0) design_seen_by(design_of(designs, slot), seen);
    } else {
      for (uint16_t i = 0; i < victims; i++) design_seen_by(design_of(designs, victim_designs[i]), seen);
    }
  }

  if ((int32_t)total_damage > 0x7ff8) total_damage = 0x7ff8;
  {
    const uint16_t fleet_id = (uint16_t)(FLEET(fseg, foff)->id | 0x8000);
    const uint16_t damage_lo = (uint16_t)total_damage;
    const bool given = minefield != 0;
    const bool own_field = mine_owner == owner;
    if (total_damage == 0) {
      if (!own_field) send_message(owner, MSG_FLEET_HIT_FIELD_UNHURT_maybe, fleet_id, fleet_id, mine_owner, type, HIT_X, HIT_Y, 0, 0);
      send_message(mine_owner, MSG_FIELD_HIT_BY_FLEET_UNHURT_maybe, fleet_id, fleet_id, type, HIT_X, HIT_Y, 0, 0, 0);
    } else if (killed == 0) {
      if (!own_field) send_message(owner, given ? MSG_FLEET_DAMAGED_GIVEN_FIELD_maybe : MSG_FLEET_DAMAGED_maybe, fleet_id, fleet_id, mine_owner, type, HIT_X, HIT_Y, damage_lo, 0);
      send_message(mine_owner, given ? MSG_FIELD_DAMAGED_FLEET_GIVEN_maybe : MSG_FIELD_DAMAGED_FLEET_maybe, fleet_id, fleet_id, type, HIT_X, HIT_Y, damage_lo, 0, 0);
    } else if ((int32_t)(int16_t)killed < total_ships) {
      if (!own_field) send_message(owner, given ? MSG_FLEET_LOST_SHIPS_GIVEN_maybe : MSG_FLEET_LOST_SHIPS_maybe, fleet_id, fleet_id, mine_owner, type, HIT_X, HIT_Y, damage_lo, killed);
      send_message(mine_owner, given ? MSG_FIELD_KILLED_SHIPS_GIVEN_maybe : MSG_FIELD_KILLED_SHIPS_maybe, fleet_id, fleet_id, type, HIT_X, HIT_Y, damage_lo, killed, 0);
    } else { /* the whole fleet is lost */
      FLEET_LOSSES.id = FLEET(fseg, foff)->id; /* 7:0x1bca takes the address of the fleet id */
      if (SALVAGE != 0) {
        const uint16_t salvage_id = FAR_REC(struct MapObject, FP_SEG(SALVAGE), FP_OFF(SALVAGE))->id_kind;
        if (!own_field) {
          const uint16_t name = (uint16_t)CALL_ORIG(7, 0x1bca, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x1cc), rt_ss);
          send_message(owner, MSG_FLEET_DESTROYED_SALVAGE_maybe, MSG_SUBJECT_SALVAGE, salvage_id, name, mine_owner, type, HIT_X, HIT_Y, 0);
        }
        send_message(mine_owner, MSG_FIELD_DESTROYED_FLEET_maybe, MSG_SUBJECT_SALVAGE, salvage_id, FLEET(fseg, foff)->id, type, HIT_X, HIT_Y, 0, 0);
      } else {
        if (!own_field) {
          const uint16_t name = (uint16_t)CALL_ORIG(7, 0x1bca, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x1cc), rt_ss);
          send_message(owner, MSG_FLEET_DESTROYED_maybe, MSG_SUBJECT_NONE, name, mine_owner, type, HIT_X, HIT_Y, 0, 0);
        }
        if (mine_owner == FLEET(fseg, foff)->owner) {
          const uint16_t name = (uint16_t)CALL_ORIG(7, 0x1bca, FAR_CDECL, 2, (uint16_t)(rt_bp - 0x1cc), rt_ss);
          send_message(mine_owner, MSG_FLEET_DESTROYED_OWN_FIELD_maybe, MSG_SUBJECT_NONE, name, type, HIT_X, HIT_Y, 0, 0, 0);
        } else {
          send_message(mine_owner, MSG_FIELD_DESTROYED_FLEET_maybe, MSG_SUBJECT_SALVAGE, FAR_REC(struct MapObject, mine_seg, mine_off)->id_kind, FLEET(fseg, foff)->id, type, HIT_X, HIT_Y, 0, 0);
        }
      }
    }
  }

  if (minefield == 0) {
    const uint32_t radius_sq = minefield_radius_sq(mine_seg, mine_off);
    if ((int32_t)radius_sq > shrink) { /* the field shrinks, and everybody the fleet belongs to knows it */
      set_minefield_radius_sq(mine_seg, mine_off, radius_sq - (uint32_t)shrink);
      minefield_seen_by(mine_seg, mine_off, (uint16_t)(1u << (owner & 0x1f)));
    } else {
      const uint16_t a[2] = {mine_off, mine_seg};
      (void)far_call(33, 0x015a, a, 2);
    }
    NEAR_AT(uint16_t, range_ptr) = (uint16_t)hit_distance;
  }
  FLEET(fseg, foff)->flags |= 0x40;
  FRAME_LEAVE();
  return 0;
}
#undef ENTRY_DIST
#undef EXIT_DIST
#undef HIT_XY
#undef HIT_X
#undef HIT_Y
#undef SALVAGE
#undef FLEET_COPY
#undef FLEET_LOSSES
/* ---- 22:0x0000 generate_turn ---- */

/* The strings and tables of the data segment that generate_turn works with. */
enum {
  DS_GAME_FILE_NAME = 0x5764,           /* the game's file name without its extension (a path) */
  DS_BACKUP_FOLDER_maybe = 0x5731,      /* text put between the folder and the file name in the backup path (empty in the static data) */
  DS_ORDERS_FILE_NAME = 0x5866,         /* g_text_buffer: the name of the orders file being read */
  DS_EXT_HST = 0x0990,                  /* "hst": 14:0x0508 loads the game with this extension */
  DSSTR_DOT_X_N = 0x099b,               /* ".x%d": the extension of a player's orders file */
  DSSTR_DOT_HST = 0x09a0,               /* ".hst" */
  DSSTR_DOT_HST_BACKUP = 0x09a5,        /* ".hst" again, for the backup path */
  DS_KEY_TABLE = 0x0980,                /* far pointer to the OrdersKey of each player (g_player_key_table) */
  DS_PLANET_WORD_TABLE_maybe = 0x0984,  /* far pointer to a word per planet, cleared when the turn starts (22:0x3ec6 reads it) */
  DS_WORK_TABLE_12000_maybe = 0x0988,   /* far pointers to two work tables of 12000 and 25000 bytes */
  DS_WORK_TABLE_25000_maybe = 0x098c
};
/* State words that only this function sets or clears. */
#define g_turn_flag_5a5a_maybe g_DAT_1110_5a5a
#define g_turn_flag_573e_maybe g_DAT_1110_573e
#define g_turn_in_progress_maybe g_DAT_1110_0970
static inline void dwr32(uint16_t off, uint32_t v) { wr32(MEM_SEL_DGROUP, off, v); }

/* Bits and values that the generation uses. */
enum {
  CURSOR_WAIT_ID = 0x7f02,              /* the hourglass (IDC_WAIT) */
  FIXED_SEED_maybe = 1234567890,        /* the random generator's seed when the UI flag below asks for a repeatable turn */
  UI_FLAG_B_REPEATABLE_maybe = 0x08,    /* g_ui_flags_b_maybe bit 3: repeatable run (fixed seed; the first player has no orders file) */
  UI_FLAG_BUSY = 0x02,                  /* bit 1 of both UI flag bytes: set while a turn is generated */
  STARTUP_OPTION_QUIT_AFTER_maybe = 0x08, /* g_startup_options bit 3: quit with exit code 1 after a successful turn */
  FILE_VERSION_NEW_MASK = 0xf000,       /* g_file_version: files with none of these bits are from an older version */
  RANDOM_TAG_VALUES = 8,                /* GameHeader.flags_81 bits 1 to 3 get a random number below 8 when the files are written */
  RANDOM_TAG_MASK = 0x0e,
  PLAYER_NUMBER_BITS = 0xf8,            /* Player.record_flags bits 3 to 7: the player's number (set for older files) */
  PLAYER_STATUS_COMPUTER_maybe = 0x02,  /* Player.status_flags bit 1: a computer player */
  ORDERS_NONE_maybe = 0x02,             /* low byte of Player.fld_54, bit 1: the player has no orders file to check */
  ORDERS_KEY_PROBLEM = 0x04,            /* the same byte, bit 2: his orders key is bad or clashes with another player's */
  FLEET_SUBJECT_FLAG = 0x8000,          /* or-ed into a fleet id to make the subject of a message "this fleet" */
  FLEET_FLAG_HIT_MINEFIELD = 0x40,      /* Fleet.flags bit 6, set by 22:0x30b6 */
  FLEET_FLAG_FOLLOWS_FLEET = 0x80,      /* Fleet.flags bit 7: the fleet's only waypoint follows another fleet (set here) */
  WAYPOINT_SPEED_BYTES_maybe = 10,      /* the bytes of a waypoint from fld_08 on (fuel and speed figures?) */
  WAYPOINT_TARGET_FLEET = 2,            /* target kind of a waypoint that follows a fleet */
  FOLLOW_PASSES = 8,                    /* the passes that resolve fleets following fleets that follow fleets */
  PLANET_FLAG_HOME_maybe = 0x04,        /* Planet.flags bit 2: the home planet of a player (cleared for all, then set for the homes) */
  DESIGN_FLAG_BIT7_maybe = 0x80,        /* ShipDesign.flags bit 7, set when 7:0x4d22 says no */
  ATTRIBUTE_SLOTS = 16,                 /* the attribute slots that 28:0x2200 clamps (Player.attrs has 15, the 16th is the next byte) */
  COST_ADJUST_DEFAULT = 15,             /* the value Player.cost_adjust_pct gets when it is out of 0 to 100 */
  COST_ADJUST_MAX = 100,
  GROWTH_RATE_MAX_maybe = 20,           /* the byte at Player+0x19, kept in 1 to 20 (the race's growth rate in percent?) */
  GROWTH_RATE_OFFSET_maybe = 0x19,
  ATTR_FIRST_ZEROED = 8,                /* the attributes 8 to 13 are zeroed one by one to repair a race */
  ATTR_LAST_ZEROED = 13,
  ATTR_FIRST_MAX_maybe = 25,            /* attributes[0] is raised up to 25 as the last step */
  MB_ICONHAND_ = 0x10,
  ERROR_CANNOT_READ_ORDERS_maybe = 1,   /* the message box 8:0x8d94 shows when an orders file cannot be read */
  PROGRESS_TABLES_READY = 370,          /* positions of the progress bar (out of 1000) */
  PROGRESS_FIRST_ORDERS = 370,
  PROGRESS_ORDERS_SPAN = 60,
  PROGRESS_ORDERS_READ = 440,
  PROGRESS_RACES = 530,
  PROGRESS_MOVEMENT = 550,
  PROGRESS_PRODUCTION = 650,
  PROGRESS_RESEARCH = 700,
  PROGRESS_SECOND_MAP_OBJECTS = 750,
  PROGRESS_FUEL = 770,
  PROGRESS_AFTER_TURN = 850,
  PROGRESS_AFTER_BATTLES = 852,
  PROGRESS_SCORES = 854,
  PROGRESS_BACKUP = 856,
  PROGRESS_BACKUP_SPAN = 17,
  PROGRESS_WRITE = 875,
  PROGRESS_WRITE_SPAN = 122,
  PROGRESS_FILES_WRITTEN = 998,
  PROGRESS_TABLES_RELEASED = 1000,
  PROGRESS_LOAD = 360
};
/* Messages that generate_turn sends. */
enum {
  MSG_ORDERS_KEY_BAD_maybe = 0x100,     /* +1 when another player has the same key */
  MSG_ORDERS_KEY_REPEATED_maybe = 0x103,
  MSG_RACE_ADJUSTED_maybe = 0x117,
  MSG_FOLLOWED_FLEET_GONE_maybe = 0x138,
  MSG_SUBJECT_PLAYERS_maybe = 0xfffb
};

/* The record in the key table (g_player_key_table, 16 bytes per player) that the orders file reader 9:0x78a0 / 9:0x649a fills. */
#pragma pack(push, 1)
struct OrdersKey {
  uint32_t key;      /* 0xffffffff before the file is read and when no file was found */
  uint8_t proof[11]; /* what the file carries besides the key; two players with the same key must carry the same bytes */
  uint8_t spare;
};
#pragma pack(pop)
_Static_assert(sizeof(struct OrdersKey) == 16, "OrdersKey is 16 bytes");
enum { ORDERS_KEY_NONE = 0xffffffffu };

static inline struct Player *player_row(uint16_t player)
{
  return FAR_REC(struct Player, MEM_SEL_DGROUP, (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player))));
}
static inline uint16_t player_row_address(uint16_t player) { return (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player))); }
/* The low byte of Player.fld_54: the orders flags above. */
static inline uint8_t *player_orders_flags(uint16_t player) { return (uint8_t *)player_row(player) + FLD(struct Player, fld_54); }
static inline struct OrdersKey *orders_key(uint16_t player)
{
  const uint32_t table = g_player_key_table;
  return FAR_REC(struct OrdersKey, FP_SEG(table), (uint16_t)(FP_OFF(table) + (uint16_t)(player * sizeof(struct OrdersKey))));
}
/* The planet a player starts at: the word at Player+8. */
static inline uint16_t player_home_planet_maybe(uint16_t player) { return drd16((uint16_t)(player_row_address(player) + 8)); }
/* The byte at Player+0x19. */
static inline int8_t *player_growth_rate_maybe(uint16_t player) { return (int8_t *)player_row(player) + GROWTH_RATE_OFFSET_maybe; }
/* The allocated size of a waypoint list: the byte at +2 of its header (struct WaypointListHdr has it as padding). */
static inline uint8_t waypoint_list_capacity_maybe(uint32_t list) { return rd8(FP_SEG(list), (uint16_t)(FP_OFF(list) + 2)); }
/* A design's rating word: the dword at ShipDesign+0x87 (struct ShipDesign has it as padding). */
static inline void set_design_rating_maybe(struct ShipDesign *design, uint32_t rating) { memcpy(design->unknown_87, &rating, sizeof rating); }

/* ---- the game's calls ---- */
static void forget_loaded_game(void) { (void)CALL_ORIG(14, 0x2992, FAR_CDECL, 0); }
static void seed_random_maybe(uint32_t seed) { (void)CALL_ORIG(8, 0x8672, FAR_CDECL, 2, (uint16_t)seed, (uint16_t)(seed >> 16)); }
static bool load_game_file(uint16_t name, uint16_t extension) { return (uint16_t)CALL_ORIG(14, 0x0508, FAR_CDECL, 2, name, extension) != 0; }
/* The catch frame of the C runtime (34:0x0800): 0 when it is set, nonzero when a Throw comes back to it. */
static uint16_t catch_here(uint16_t buffer) { return (uint16_t)CALL_ORIG(34, 0x0800, FAR_CDECL, 1, buffer); }
static void show_progress(uint16_t position) { (void)CALL_ORIG(8, 0xb90e, FAR_CDECL, 1, position); }
/* The share of a progress step that each of the players and the host file gets: step / (player count + 1). */
static uint16_t progress_share(uint16_t step) { return (uint16_t)((int16_t)step / (int16_t)(uint16_t)(g_player_count + 1)); }
static uint32_t heap_alloc_zone6(uint16_t size)
{
  const uint16_t a[2] = {size, 6};
  return far_call(12, 0x0232, a, 2);
}
static void heap_free_zone6(uint16_t table)
{
  const uint32_t block = drd32(table);
  const uint16_t a[3] = {FP_OFF(block), FP_SEG(block), 6};
  (void)far_call(12, 0x04d0, a, 3);
  dwr32(table, 0);
}
static void remove_file(uint16_t path) { (void)CALL_ORIG(34, 0x0922, FAR_CDECL, 1, path); }
static void rename_file(uint16_t from, uint16_t to) { (void)CALL_ORIG(34, 0x08f6, FAR_CDECL, 2, from, to); }
static bool file_exists(uint16_t path) { return (uint16_t)CALL_ORIG(34, 0x084e, FAR_CDECL, 2, path, 0) != 0xffff; }
static void copy_file(uint16_t from, uint16_t to) { (void)CALL_ORIG(8, 0x8c92, FAR_CDECL, 2, from, to); }
static uint16_t last_backslash(uint16_t path) { return (uint16_t)CALL_ORIG(34, 0x0578, FAR_CDECL, 2, path, '\\'); }
static uint16_t race_points_maybe(uint16_t player) { return (uint16_t)CALL_ORIG(28, 0x2d30, FAR_CDECL, 1, player_row_address(player)); }
/* Zero-terminated strings in the data segment (the stack is in the same segment): length, copy and append. */
static uint16_t ds_length(uint16_t at)
{
  uint16_t n = 0;
  while (drd8((uint16_t)(at + n)) != 0) n++;
  return n;
}
static void ds_copy(uint16_t to, uint16_t from)
{
  memcpy(LIN(MK_FP(MEM_SEL_DGROUP, to)), LIN(MK_FP(MEM_SEL_DGROUP, from)), (size_t)ds_length(from) + 1);
}
static void ds_append(uint16_t to, uint16_t from) { ds_copy((uint16_t)(to + ds_length(to)), from); }

/* _wsprintf(DS:dest, DS:format, words): the original pushes the variable words first and the two far pointers below them. */
static void format_ds(uint16_t dest, uint16_t format, const uint16_t *words, uint16_t count)
{
  const uint16_t sp0 = rt_sp;
  uint16_t sp = sp0;
  for (uint16_t i = count; i > 0; i--) {
    sp = (uint16_t)(sp - 2);
    wr16(rt_ss, sp, words[i - 1]);
  }
  sp = (uint16_t)(sp - 2);
  wr16(rt_ss, sp, MEM_SEL_DGROUP);
  sp = (uint16_t)(sp - 2);
  wr16(rt_ss, sp, format);
  sp = (uint16_t)(sp - 2);
  wr16(rt_ss, sp, MEM_SEL_DGROUP);
  sp = (uint16_t)(sp - 2);
  wr16(rt_ss, sp, dest);
  rt_sp = sp;
  (void)_wsprintf(MK_FP(MEM_SEL_DGROUP, dest), MK_FP(MEM_SEL_DGROUP, format), MK_FP(rt_ss, (uint16_t)(sp + 8)));
  rt_sp = sp0;
}

/* The catch buffer in the frame (18 bytes at BP-0x4a). */
static uint16_t catch_buffer_address(void) { return (uint16_t)(rt_bp - 0x4a); }

/* ---- loading ---- */

/* Files from an older version have no player number in Player.record_flags: when none of the players has one, the players are numbered. */
static void number_players_of_old_file(void)
{
  if ((g_file_version & FILE_VERSION_NEW_MASK) != 0) return;
  uint16_t player = 0;
  for (; (int16_t)player < (int16_t)g_player_count; player++)
    if ((player_row(player)->record_flags & PLAYER_NUMBER_BITS) != 0) break;
  if (player != g_player_count) return;
  for (player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    struct Player *row = player_row(player);
    row->record_flags = (uint8_t)((row->record_flags & ~PLAYER_NUMBER_BITS) | ((player << 3) & PLAYER_NUMBER_BITS));
  }
}

/* The work tables of the turn: two blocks of 12000 and 25000 bytes, a cleared word per planet, and the keys of the players' orders. */
static void allocate_turn_tables(void)
{
  dwr32(DS_WORK_TABLE_12000_maybe, heap_alloc_zone6(12000));
  dwr32(DS_WORK_TABLE_25000_maybe, heap_alloc_zone6(25000));
  const uint16_t planet_bytes = (uint16_t)(g_planet_count_in_game + g_planet_count_in_game);
  const uint32_t words = heap_alloc_zone6(planet_bytes);
  dwr32(DS_PLANET_WORD_TABLE_maybe, words);
  memset(LIN(words), 0, planet_bytes);
  dwr32(DS_KEY_TABLE, heap_alloc_zone6((uint16_t)(g_player_count << 4)));
}
static void release_turn_tables(void)
{
  heap_free_zone6(DS_PLANET_WORD_TABLE_maybe);
  heap_free_zone6(DS_KEY_TABLE);
  heap_free_zone6(DS_WORK_TABLE_12000_maybe);
  heap_free_zone6(DS_WORK_TABLE_25000_maybe);
}

/* Read the orders file of every player (9:0x78a0 finds and reads it, 9:0x649a checks it) into the key table. False when a file
 * cannot be read (after the message box). */
static bool read_orders_files(void)
{
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    const uint16_t words[3] = {DS_GAME_FILE_NAME, MEM_SEL_DGROUP, (uint16_t)(player + 1)};
    format_ds(DS_ORDERS_FILE_NAME, DSSTR_X_N, words, 3);
    g_current_player = player;
    orders_key(player)->key = ORDERS_KEY_NONE;
    if ((uint16_t)CALL_ORIG(9, 0x78a0, FAR_CDECL, 1, DS_ORDERS_FILE_NAME) != 0 && (uint16_t)CALL_ORIG(9, 0x649a, FAR_CDECL, 0) == 0) {
      (void)CALL_ORIG(8, 0x8d94, FAR_CDECL, 2, ERROR_CANNOT_READ_ORDERS_maybe, MB_ICONHAND_);
      return false;
    }
    show_progress((uint16_t)(MulDiv(PROGRESS_ORDERS_SPAN, (int16_t)(player + 1), (int16_t)g_player_count) + PROGRESS_FIRST_ORDERS));
  }
  return true;
}

/* A player whose orders file is looked at: he is not marked as having none and is not a computer player. */
static bool player_expects_orders(uint16_t player)
{
  return (*player_orders_flags(player) & ORDERS_NONE_maybe) == 0 && (player_row(player)->status_flags & PLAYER_STATUS_COMPUTER_maybe) == 0;
}
/* Check the keys of the orders files: 14:0x2bb4 refuses a key it does not accept, and two players with the same key must carry
 * the same bytes after it. Players whose key is refused or who clash with an earlier player get ORDERS_KEY_PROBLEM. */
static void check_orders_keys(void)
{
  const bool first_player_has_no_orders = (g_ui_flags_b_maybe & UI_FLAG_B_REPEATABLE_maybe) != 0;
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    uint8_t *flags = player_orders_flags(player);
    if (!player_expects_orders(player) || (first_player_has_no_orders && player == 0)) {
      *flags = (uint8_t)(*flags & ~ORDERS_KEY_PROBLEM);
      continue;
    }
    const struct OrdersKey *mine = orders_key(player);
    if (mine->key != ORDERS_KEY_NONE) {
      if ((uint16_t)CALL_ORIG(14, 0x2bb4, FAR_CDECL, 2, (uint16_t)mine->key, (uint16_t)(mine->key >> 16)) == 0) {
        *flags = (uint8_t)(*flags | ORDERS_KEY_PROBLEM);
        continue;
      }
    } else {
      continue;
    }
    *flags = (uint8_t)(*flags & ~ORDERS_KEY_PROBLEM);
    for (uint16_t earlier = 0; earlier < player; earlier++) {
      const struct OrdersKey *other = orders_key(earlier);
      if (player_expects_orders(earlier) && other->key == mine->key && memcmp(mine->proof, other->proof, sizeof mine->proof) != 0) {
        *player_orders_flags(earlier) = (uint8_t)(*player_orders_flags(earlier) | ORDERS_KEY_PROBLEM);
        *flags = (uint8_t)(*flags | ORDERS_KEY_PROBLEM);
      }
    }
  }
}
/* Tell the players with a key problem (3:0x04a2 names the player they clash with, or 0xffff). */
static void report_orders_key_problems(void)
{
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    if ((*player_orders_flags(player) & ORDERS_KEY_PROBLEM) == 0) continue;
    const uint16_t clash = (uint16_t)far_call(3, 0x04a2, &player, 1);
    send_message2(player, (uint16_t)(MSG_ORDERS_KEY_BAD_maybe + (clash != NO_PLAYER ? 1 : 0)), MSG_SUBJECT_PLAYERS_maybe, clash, 0);
    if (g_current_turn > 10 && (g_current_turn & 7) == (player & 7)) send_message2(player, MSG_ORDERS_KEY_REPEATED_maybe, MSG_SUBJECT_PLAYERS_maybe, 0, 0);
  }
}

/* Every design in use gets its first slot back to category 1, item 1 and at least one part. */
static void repair_design_first_slots(void)
{
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    const uint32_t designs = player_designs(player);
    for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
      struct ShipDesign *design = design_of(designs, slot);
      if ((design->flags & DESIGN_FLAG_FREE) != 0 || design->slots[0].category == 1) continue;
      design->slots[0].category = 1;
      design->slots[0].item = 1;
      if (design->slots[0].count < 1) design->slots[0].count = 1;
    }
  }
}

/* ---- fleets that follow fleets ---- */

/* A fleet whose only waypoint follows another fleet. */
static bool fleet_only_follows_fleet(uint16_t fseg, uint16_t foff)
{
  return FLEET(fseg, foff)->waypoint_count == 1 && waypoint_target_kind(fleet_waypoint(fseg, foff, 0)) == WAYPOINT_TARGET_FLEET;
}
/* Clear the minefield flag of every fleet and mark the fleets that follow a fleet (FLEET_FLAG_FOLLOWS_FLEET). True when there is one. */
static bool mark_fleets_that_follow_fleets(void)
{
  bool any = false;
  for (uint16_t index = 0; (int16_t)index < (int16_t)g_fleet_count; index++) {
    const uint32_t fleet = fleet_ptr_at((uint16_t)(index << 2));
    if (fleet == 0) break;
    struct Fleet *f = FLEET(FP_SEG(fleet), FP_OFF(fleet));
    f->flags = (uint8_t)(f->flags & ~FLEET_FLAG_HIT_MINEFIELD);
    if (fleet_only_follows_fleet(FP_SEG(fleet), FP_OFF(fleet))) {
      any = true;
      f->flags = (uint8_t)(f->flags | FLEET_FLAG_FOLLOWS_FLEET);
    } else {
      f->flags = (uint8_t)(f->flags & ~FLEET_FLAG_FOLLOWS_FLEET);
    }
  }
  return any;
}
/* The follower gets two waypoints: the second is the followed fleet's second waypoint, with the speed fields of its own first. */
static void give_follower_the_route(uint16_t fseg, uint16_t foff, uint16_t tseg, uint16_t toff)
{
  uint32_t list = FLEET(fseg, foff)->waypoints;
  if (waypoint_list_capacity_maybe(list) <= 1) {
    const uint16_t a[3] = {FP_OFF(list), FP_SEG(list), 2};
    FLEET(fseg, foff)->waypoints = far_call(12, 0x0528, a, 3);
  }
  struct Waypoint *second = fleet_waypoint(fseg, foff, 1);
  *second = *fleet_waypoint(tseg, toff, 1);
  memcpy((uint8_t *)second + FLD(struct Waypoint, fld_08), (uint8_t *)fleet_waypoint(fseg, foff, 0) + FLD(struct Waypoint, fld_08), WAYPOINT_SPEED_BYTES_maybe);
  FLEET(fseg, foff)->waypoint_count = 2;
  list = FLEET(fseg, foff)->waypoints;
  FAR_REC(struct WaypointListHdr, FP_SEG(list), FP_OFF(list))->count = 2;
}
/* A follower gets the route of the fleet it follows once that fleet has a route of its own. A follower whose fleet is gone, or
 * cannot be followed, is told so and stops following. Up to FOLLOW_PASSES passes handle chains of followers. */
static void resolve_fleets_that_follow_fleets(void)
{
  bool changed = true;
  for (uint16_t pass = 0; pass < FOLLOW_PASSES && changed; pass++) {
    changed = false;
    for (uint16_t index = 0; (int16_t)index < (int16_t)g_fleet_count; index++) {
      const uint32_t fleet = fleet_ptr_at((uint16_t)(index << 2));
      if (fleet == 0) break;
      const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
      struct Fleet *f = FLEET(fseg, foff);
      if ((f->flags & FLEET_FLAG_FOLLOWS_FLEET) == 0 || f->waypoint_count != 1) continue;
      const struct Waypoint here = *fleet_waypoint(fseg, foff, 0);
      if (waypoint_target_kind(&here) != WAYPOINT_TARGET_FLEET) {
        f->flags = (uint8_t)(f->flags & ~FLEET_FLAG_FOLLOWS_FLEET);
        continue;
      }
      const uint32_t followed = fleet_by_id(here.target_id);
      const uint16_t tseg = FP_SEG(followed), toff = FP_OFF(followed);
      if (followed == 0 || (FLEET(tseg, toff)->waypoint_count == 1 && waypoint_target_kind(fleet_waypoint(tseg, toff, 0)) != WAYPOINT_TARGET_FLEET)) {
        send_message(f->owner, MSG_FOLLOWED_FLEET_GONE_maybe, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, 0, 0, 0, 0, 0, 0);
        f->flags = (uint8_t)(f->flags & ~FLEET_FLAG_FOLLOWS_FLEET);
        continue;
      }
      if (FLEET(tseg, toff)->waypoint_count == 1) continue; /* it follows a fleet too: a later pass */
      changed = true;
      give_follower_the_route(fseg, foff, tseg, toff);
    }
  }
}

/* ---- the turn ---- */

/* A race whose advantage points (28:0x2d30) are negative is repaired for a human player, step by step until they are not:
 * the growth rate goes down to 1, then the attributes 8 to 13 are zeroed one by one, then attribute 0 is raised up to 25. */
static void repair_player_race(uint16_t player)
{
  struct Player *row = player_row(player);
  for (uint16_t attribute = 0; attribute < ATTRIBUTE_SLOTS; attribute++) {
    const uint16_t value = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, player_row_address(player), attribute);
    (void)CALL_ORIG(28, 0x2200, FAR_CDECL, 3, player_row_address(player), attribute, value); /* writes it back inside its limits */
  }
  if (row->cost_adjust_pct < 0 || row->cost_adjust_pct > COST_ADJUST_MAX) row->cost_adjust_pct = COST_ADJUST_DEFAULT;
  int8_t *growth = player_growth_rate_maybe(player);
  if (*growth < 0) *growth = 1;
  if (*growth > GROWTH_RATE_MAX_maybe) *growth = GROWTH_RATE_MAX_maybe;
  int16_t points = (int16_t)race_points_maybe(player);
  if (points >= 0 || (row->status_flags & PLAYER_STATUS_COMPUTER_maybe) != 0) return;
  send_message2(player, MSG_RACE_ADJUSTED_maybe, NO_PLAYER, 0, 0);
  while (*growth > 1) {
    (*growth)--;
    points = (int16_t)race_points_maybe(player);
    if (points >= 0) break;
  }
  if (points >= 0) return;
  for (uint16_t attribute = ATTR_FIRST_ZEROED; attribute <= ATTR_LAST_ZEROED; attribute++) {
    row->attrs[attribute] = 0;
    points = (int16_t)race_points_maybe(player);
    if (points >= 0) break;
  }
  if (points >= 0) return;
  while (row->attrs[0] < ATTR_FIRST_MAX_maybe) {
    row->attrs[0]++;
    points = (int16_t)race_points_maybe(player);
    if (points >= 0) break;
  }
}

/* Every planet loses the home flag, then the home planet of every player gets it. */
static void flag_home_planets(void)
{
  const uint16_t seg = g_planet_table_segment_maybe;
  const uint16_t end = (uint16_t)(g_planet_table + (uint16_t)((int16_t)g_planet_count * (int16_t)sizeof(struct Planet)));
  for (uint16_t at = g_planet_table; at < end; at = (uint16_t)(at + sizeof(struct Planet)))
    PLANET(seg, at)->flags = (uint8_t)(PLANET(seg, at)->flags & ~PLANET_FLAG_HOME_maybe);
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) {
    const uint16_t at = (uint16_t)(g_planet_table + (uint16_t)((int16_t)player_home_planet_maybe(player) * (int16_t)sizeof(struct Planet)));
    PLANET(seg, at)->flags = (uint8_t)(PLANET(seg, at)->flags | PLANET_FLAG_HOME_maybe);
  }
}

/* The parts of the turn between the orders and the scores. */
static void run_turn_phases(void)
{
  show_progress(PROGRESS_ORDERS_READ);
  const uint16_t first_pass = 0, second_pass = 1;
  (void)far_call(22, 0x0e92, &first_pass, 1); /* run_fleet_order_phases(0) */
  show_progress(PROGRESS_RACES);
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) repair_player_race(player);
  (void)CALL_ORIG(23, 0x4592, FAR_CDECL, 0);
  (void)far_call(22, 0x0f6e, &first_pass, 1); /* move_map_objects(0) */
  show_progress(PROGRESS_MOVEMENT);
  (void)far_call(22, 0x1f18, NULL, 0); /* move_fleets */
  flag_home_planets();
  show_progress(PROGRESS_PRODUCTION);
  (void)CALL_ORIG(23, 0x42a0, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x4aa4, FAR_CDECL, 0);
  show_progress(PROGRESS_RESEARCH);
  (void)CALL_ORIG(23, 0x0000, FAR_CDECL, 0);
  show_progress(PROGRESS_SECOND_MAP_OBJECTS);
  (void)far_call(22, 0x0f6e, &second_pass, 1); /* move_map_objects(1) */
  show_progress(PROGRESS_FUEL);
  (void)far_call(22, 0x1cfa, NULL, 0);         /* regenerate_fleet_fuel */
  (void)far_call(22, 0x0e92, &second_pass, 1); /* run_fleet_order_phases(1) */
  (void)CALL_ORIG(23, 0x45c4, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x28de, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x2ba4, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x2dc0, FAR_CDECL, 0);
  show_progress(PROGRESS_AFTER_TURN);
  (void)CALL_ORIG(30, 0x1104, FAR_CDECL, 0);
  (void)CALL_ORIG(7, 0x42c8, FAR_CDECL, 0);
  (void)CALL_ORIG(23, 0x31da, FAR_CDECL, 0);
  show_progress(PROGRESS_AFTER_BATTLES);
  (void)CALL_ORIG(14, 0x54ea, FAR_CDECL, 4, 2, NO_PLAYER, 1, 0);
  (void)CALL_ORIG(23, 0x40f4, FAR_CDECL, 0);
  g_current_turn++;
}

/* ---- the scores and the files ---- */

/* The buffer for the backup path: 0x100 bytes at BP-0x15e. */
static uint16_t backup_path_address(void) { return (uint16_t)(rt_bp - 0x15e); }
/* The path of the backup copy of the game file in `backup`: the game file's folder, then DS_BACKUP_FOLDER_maybe, then the file's name. */
static void build_backup_path(uint16_t backup)
{
  const uint16_t slash = last_backslash(DS_GAME_FILE_NAME);
  if (slash == 0) {
    ds_copy(backup, DS_BACKUP_FOLDER_maybe);
    ds_append(backup, DS_GAME_FILE_NAME);
  } else {
    ds_copy(backup, DS_GAME_FILE_NAME);
    dwr8((uint16_t)(backup + (uint16_t)(slash - DS_GAME_FILE_NAME) + 1), 0); /* keep the folder and its backslash */
    ds_append(backup, DS_BACKUP_FOLDER_maybe);
    ds_append(backup, (uint16_t)(slash + 1));
  }
}

/* Out words of 7:0x336a, a design's rating parts. */
#define RATING_PART_A L16(-0x20)
#define RATING_PART_B L16(-0x1e)
#define RATING_PART_C L16(-0x16)
/* The rating word of every design in use: for a starbase design the square of (100 - 9:0x55f4), for a ship design the parts of 7:0x336a
 * packed into 11, 11, 7 and 3 bits; 7:0x4d22 decides whether the design gets ShipDesign.flags bit 7. */
static void rate_designs(uint16_t player)
{
  const uint32_t starbases = drd32((uint16_t)(DS_SB_DESIGNS + (uint16_t)(player << 2)));
  for (uint16_t slot = 0; slot < STARBASE_DESIGNS_PER_PLAYER; slot++) {
    struct ShipDesign *design = design_of(starbases, slot);
    if ((design->flags & DESIGN_FLAG_FREE) != 0) continue;
    const uint16_t at = (uint16_t)(FP_OFF(starbases) + (uint16_t)(slot * sizeof(struct ShipDesign)));
    const uint16_t answer = (uint16_t)CALL_ORIG(9, 0x55f4, FAR_CDECL, 4, at, FP_SEG(starbases), player, 0);
    const int32_t shortfall = (int16_t)(uint16_t)(100 - answer);
    set_design_rating_maybe(design, (uint32_t)(shortfall * shortfall));
  }
  const uint32_t ships = player_designs(player);
  for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
    struct ShipDesign *design = design_of(ships, slot);
    if ((design->flags & DESIGN_FLAG_FREE) != 0) continue;
    const uint16_t at = (uint16_t)(FP_OFF(ships) + (uint16_t)(slot * sizeof(struct ShipDesign)));
    const uint16_t whole = (uint16_t)CALL_ORIG(7, 0x336a, FAR_CDECL, 6, at, FP_SEG(ships), player, (uint16_t)(rt_bp - 0x20), (uint16_t)(rt_bp - 0x1e),
                                              (uint16_t)(rt_bp - 0x16));
    set_design_rating_maybe(design, (uint32_t)(whole & 0x7ff) | ((uint32_t)((uint16_t)RATING_PART_A & 0x7ff) << 11) |
                                        ((uint32_t)((uint16_t)RATING_PART_B & 0x7f) << 22) | ((uint32_t)((uint16_t)RATING_PART_C & 7) << 29));
    if ((uint16_t)CALL_ORIG(7, 0x4d22, FAR_CDECL, 3, at, FP_SEG(ships), player) == 0) design->flags = (uint8_t)(design->flags | DESIGN_FLAG_BIT7_maybe);
  }
}

/* The old files of the game go to the backup folder (a missing orders file is remembered: the player's turn file is copied then,
 * not moved), the host file last. */
static void back_up_old_files(uint16_t name_end, uint16_t backup, uint16_t backup_end, uint8_t *had_no_orders_file)
{
  uint16_t progress = PROGRESS_BACKUP;
  for (uint16_t slot = 0;; slot++) {
    show_progress(progress);
    progress = (uint16_t)(progress + progress_share(PROGRESS_BACKUP_SPAN));
    const bool host_file = (int16_t)slot >= (int16_t)g_player_count;
    if (host_file) {
      ds_copy(name_end, DSSTR_DOT_HST);
      ds_copy(backup_end, DSSTR_DOT_HST_BACKUP);
    } else {
      const uint16_t number[1] = {(uint16_t)(slot + 1)};
      format_ds(name_end, DSSTR_DOT_X_N, number, 1);
      ds_copy(backup_end, name_end);
      remove_file(backup);
      if (!file_exists(DS_GAME_FILE_NAME))
        had_no_orders_file[slot] = 1;
      else
        rename_file(DS_GAME_FILE_NAME, backup);
      dwr8((uint16_t)(backup_end + 1), 'm'); /* the player's turn file: ".m%d" */
      dwr8((uint16_t)(name_end + 1), 'm');
    }
    remove_file(backup);
    if (!host_file && had_no_orders_file[slot])
      copy_file(DS_GAME_FILE_NAME, backup);
    else
      rename_file(DS_GAME_FILE_NAME, backup);
    dwr8(name_end, 0);
    if (host_file) break;
  }
}

/* Write the files of the turn with 14:0x35b0: one for each player, then the host file. */
static void write_new_files(const uint8_t *had_no_orders_file)
{
  uint16_t progress = PROGRESS_WRITE;
  struct GameHeader *header = FAR_REC(struct GameHeader, MEM_SEL_DGROUP, DS_GAME_HEADER);
  const uint16_t tag = rand_below(RANDOM_TAG_VALUES);
  header->flags_81 = (uint8_t)((header->flags_81 & ~RANDOM_TAG_MASK) | ((tag << 1) & RANDOM_TAG_MASK)); /* a random tag in bits 1 to 3 */
  for (uint16_t slot = 0;; slot++) {
    show_progress(progress);
    progress = (uint16_t)(progress + progress_share(PROGRESS_WRITE_SPAN));
    const bool host_file = (int16_t)g_player_count <= (int16_t)slot;
    const uint16_t file = host_file ? PLAYER_INDEX_HOST : slot;
    const uint16_t no_orders = (!host_file && had_no_orders_file[slot]) ? 1 : 0;
    (void)CALL_ORIG(14, 0x35b0, FAR_CDECL, 3, DS_GAME_FILE_NAME, file, no_orders);
    if (host_file) break;
  }
}

/* The turn after the orders are read: the follow orders, the phases, the scores, the backups and the new files. */
static void generate_and_write_turn(void)
{
  const bool followers = mark_fleets_that_follow_fleets();
  (void)CALL_ORIG(7, 0x42c8, FAR_CDECL, 0);
  if (followers) resolve_fleets_that_follow_fleets();
  run_turn_phases();
  const uint16_t name_end = (uint16_t)(DS_GAME_FILE_NAME + ds_length(DS_GAME_FILE_NAME));
  const uint16_t backup = backup_path_address();
  build_backup_path(backup);
  const uint16_t backup_end = (uint16_t)(backup + ds_length(backup));
  show_progress(PROGRESS_SCORES);
  (void)CALL_ORIG(23, 0x3b78, FAR_CDECL, 0);
  for (uint16_t player = 0; (int16_t)player < (int16_t)g_player_count; player++) rate_designs(player);
  uint8_t had_no_orders_file[MAX_PLAYERS] = {0};
  back_up_old_files(name_end, backup, backup_end, had_no_orders_file);
  write_new_files(had_no_orders_file);
  show_progress(PROGRESS_FILES_WRITTEN);
  g_turn_in_progress_maybe = 0;
}

/* generate_turn: generate one turn of the game whose name is in DS:0x5764.
 * 22:0x0000 FUN_10a8_0000, FAR CDECL (no argument), AX = 1 when the turn was generated and written, 0 when the game could not be
 * loaded or an orders file could not be read, 3694 bytes (ENTER 0x16a,0; PUSH DI; PUSH SI); USER.LoadCursor / SetCursor / _wsprintf,
 * GDI.MulDiv.
 * Shows the hourglass, forgets the loaded game (14:0x2992) and loads the host file (14:0x0508). Within a catch (34:0x0800) it
 * allocates the work tables, reads the orders file of each player (9:0x78a0, 9:0x649a) with a progress bar (8:0xb90e, positions out of
 * 1000), checks the orders keys and tells the players whose key is bad or clashes (3:0x04a2, 6:0x7328), repairs the designs' first
 * slots, lets the fleets that follow fleets take their route, and then runs the turn: the waypoint orders (22:0x0e92), the races'
 * repair, the map objects (22:0x0f6e), the movement (22:0x1f18), production and research of segment 23, the fuel (22:0x1cfa), the
 * battles (30:0x1104) and the scores (23:0x3b78); every design gets its rating word; the old files are moved to the backup folder and the
 * new ones written (14:0x35b0). The tables are freed whatever happened, the UI flags and the hourglass restored.
 * Not restored: g_catch_buffer (DS:0x6e keeps the address of this frame's catch buffer) and g_current_player (set to 0xffff). */
DECOMP(22, 0x0000, generate_turn, FAR_CDECL)
uint16_t generate_turn(void)
{
  FRAME_ENTER_REGS(0x16a, 2);
  uint16_t generated = 0;
  const uint16_t old_cursor = (uint16_t)SetCursor(LoadCursor(0, CURSOR_WAIT_ID));
  forget_loaded_game();
  if ((g_ui_flags_b_maybe & UI_FLAG_B_REPEATABLE_maybe) != 0) seed_random_maybe(FIXED_SEED_maybe);
  const uint16_t was_computer_turn = g_computer_turns_running;
  g_computer_turns_running = 1;
  show_progress(PROGRESS_LOAD);
  const bool loaded = load_game_file(DS_GAME_FILE_NAME, DS_EXT_HST);
  g_computer_turns_running = was_computer_turn;
  if (!loaded) {
    (void)SetCursor(old_cursor);
    FRAME_LEAVE();
    return 0;
  }
  number_players_of_old_file();
  g_catch_buffer = catch_buffer_address();
  if (catch_here(catch_buffer_address()) == 0) {
    allocate_turn_tables();
    show_progress(PROGRESS_TABLES_READY);
    g_ui_flags_a_maybe = (uint8_t)(g_ui_flags_a_maybe | UI_FLAG_BUSY);
    g_ui_flags_b_maybe = (uint8_t)(g_ui_flags_b_maybe | UI_FLAG_BUSY);
    g_turn_flag_5a5a_maybe = 0;
    g_turn_flag_573e_maybe = 0;
    g_message_buffer_used = 0;
    if (read_orders_files()) {
      g_current_player = NO_PLAYER;
      check_orders_keys();
      report_orders_key_problems();
      repair_design_first_slots();
      generate_and_write_turn();
      generated = 1;
    }
  }
  show_progress(PROGRESS_TABLES_RELEASED);
  release_turn_tables();
  g_ui_flags_a_maybe = (uint8_t)(g_ui_flags_a_maybe & ~UI_FLAG_BUSY);
  g_ui_flags_b_maybe = (uint8_t)(g_ui_flags_b_maybe & ~UI_FLAG_BUSY);
  g_current_player = NO_PLAYER;
  if (generated != 0 && (g_startup_options & STARTUP_OPTION_QUIT_AFTER_maybe) != 0) g_quit_exit_code = 1;
  (void)SetCursor(old_cursor);
  FRAME_LEAVE();
  return generated;
}

/* ---- 22:0x3ec6 execute_waypoint_orders ---- */

enum {
  /* Waypoint.flags bits 0 to 3: the task of a waypoint */
  TASK_NONE = 0,
  TASK_TRANSPORT = 1,
  TASK_COLONIZE = 2,
  TASK_REMOTE_MINING = 3,
  TASK_MERGE = 4,
  TASK_SCRAP = 5,
  TASK_LAY_MINES = 6,
  TASK_ROUTE = 8,
  TASK_TRANSFER = 9,
  /* Waypoint.flags bits 8 to 11: what the waypoint points at */
  TARGET_PLANET = 1,
  TARGET_FLEET = 2,
  TARGET_POINT = 4,
  TARGET_OBJECT = 8,
  /* the five cargo orders of a transport waypoint: three minerals, the colonists and the fuel */
  CARGO_COLONISTS = 3,
  CARGO_FUEL = 4,
  CARGO_ORDERS = 5,
  CARGO_MINERALS = 3,
  /* the top nibble of a cargo order word: what to do; the low 12 bits are the amount or the percentage */
  ORDER_LOAD_ALL = 1,
  ORDER_UNLOAD_ALL = 2,
  ORDER_LOAD_EXACT = 3,
  ORDER_UNLOAD_EXACT = 4,
  ORDER_FILL_TO_PERCENT = 5,
  ORDER_WAIT_FOR_PERCENT = 6,
  ORDER_LOAD_DUNNAGE = 7,
  ORDER_SET_AMOUNT = 8,
  ORDER_SET_WAYPOINT = 9,
  ORDER_TYPE_SHIFT = 12,
  ORDER_AMOUNT_MASK = 0x0fff,
  /* the phases of a turn in which the orders run: the odd ones unload, the even ones load */
  PHASE_FIRST = 1,
  PHASE_REMOTE_MINING = 3,
  PHASE_LAST = 4,
  /* the byte at Fleet+0x76 (22:0x1f18 uses its low nibble and bit 4) */
  FLEET_STATE_ORDERS_DONE_maybe = 0x80,
  FLEET_STATE_PHASE_BITS = 0x60,
  FLEET_FLAG_AT_PLANET_maybe = 0x20, /* Fleet.flags bit 5: the fleet is at a planet (22:0x1f18 calls it "still to move") */
  PLANET_FLAG_OPEN_maybe = 0x20,     /* Planet.flags bit 5: an unowned planet can be invaded only with it set */
  PLAYER_RACE_NO_INVASION_maybe = 8, /* Player.attrs[14]: this race cannot invade or remote mine */
  PLAYER_RACE_MINE_LAYER_maybe = 5,  /* this race lays mines on the way */
  PLAYER_TRAIT_SCRAP_BONUS_maybe = 5, /* 28:0x2230 trait numbers */
  PLAYER_TRAIT_COPY_DESIGNS_maybe = 12,
  RELATION_ENEMY_maybe = 2,
  FUEL_CAPACITY = 1, /* 10:0x292a(fleet, which) */
  CARGO_CAPACITY = 2,
  CAPACITY_CAP = 2000000,    /* a percentage order works on at most this capacity */
  MINEFIELD_FULL = 1000000,  /* a minefield of this size takes no more mines */
  MINE_DISTANCE_START = 10000000,
  MINE_WAYPOINT_FOREVER = 5, /* the counter of a lay-mines waypoint that does not run out */
  SALVAGE_TYPE_maybe = 0x20, /* the MapObject kind that holds minerals to load */
  SCRAP_FIELDS = 3,          /* the minerals a scrapped fleet leaves */
  UI_FLAG_E_SCRAPPING_maybe = 0x08, /* g_ui_flags_e_maybe bit 3 while a fleet is scrapped */
  PART_CATEGORY_COLONY_maybe = 0x1000,
  SCRAP_LOOP_END_maybe = 3,         /* the loop counter over the minerals that the original leaves in a stack slot a message reads */
  DS_DESIGN_TALLY_maybe = 0x27d6,
  DESIGN_TALLY_BYTES_maybe = 19,
  PLAYER_NO_TRANSFER_maybe = 0x01,  /* low byte of Player.fld_54, bit 0: the player cannot be given a fleet */
  FLEETS_PER_PLAYER_MAX_maybe = 0x200,
  DESIGN_SLOT_BITS_maybe = 0x7c     /* ShipDesign.flags bits 2 to 6: the slot the design is in */
};
/* The message ids of 22:0x3ec6; the wording is in the game's string table, which was not read here. */
enum {
  MSG_LOADED_maybe = 0x2b,                      /* +1 for colonists */
  MSG_UNLOADED_maybe = 0x2d,                    /* +1 for colonists */
  MSG_ORDER_DONE_maybe = 0x4e,                  /* the fleet has done its last order */
  MSG_COLONIZE_NO_PLANET_maybe = 0x51,
  MSG_COLONIZE_OWNED_maybe = 0x52,
  MSG_COLONIZE_NO_COLONISTS_maybe = 0x53,
  MSG_COLONIZE_NO_MODULE_maybe = 0x54,
  MSG_INVADE_NOT_OPEN_maybe = 0x55,
  MSG_INVADE_RACE_maybe = 0x56,
  MSG_SCRAPPED_maybe = 0x59,                    /* +1 with a starbase */
  MSG_SCRAP_IN_SPACE_maybe = 0x5b,
  MSG_SCRAP_RESOURCES_maybe = 0x5c,             /* +1 with a starbase */
  MSG_FUEL_SHORT_maybe = 0x3c,
  MSG_FUEL_SHORT_FOR_TANK_maybe = 0x3d,
  MSG_REMOTE_MINING_NO_RATE_maybe = 0x75,
  MSG_REMOTE_MINING_INHABITED_maybe = 0x76,
  MSG_REMOTE_MINING_IN_SPACE_maybe = 0x77,
  MSG_LOADED_VIA_PLANET_maybe = 0x7d,
  MSG_NO_MINES_maybe = 0xbf,
  MSG_MINES_LAID_maybe = 0xc3,
  MSG_MINES_ADDED_maybe = 0xc4,
  MSG_MERGE_NO_TARGET_maybe = 0xf5,
  MSG_MERGE_OTHER_PLAYER_maybe = 0xf6,
  MSG_MERGED_maybe = 0xf7,
  MSG_LOADED_FOREIGN_maybe = 0x119,
  MSG_NOT_SALVAGE_maybe = 0x11e,
  MSG_CANT_LOAD_PLANET_maybe = 0x11f,
  MSG_CANT_LOAD_FLEET_maybe = 0x120,
  MSG_NOT_ENOUGH_FOR_SET_AMOUNT_maybe = 0x121,  /* +1 for colonists */
  MSG_CANT_LOAD_POINT_maybe = 0x123,
  MSG_NO_FUEL_LEFT_maybe = 0x126,
  MSG_ROUTE_maybe = 0x127,                      /* +1 when the next leg has no warp */
  MSG_SCRAP_OWNER_LESS_maybe = 0x13c,           /* what the owner of the planet is told about a scrap: 0x13c / 0x13d with an answer of 30:0x6112, */
  MSG_SCRAP_OWNER_MORE_maybe = 0x13d,           /* 0x13e / 0x13f when he also gets resources, else 0x140 and 0x142 (+1 with a starbase) */
  MSG_SCRAP_OWNER_RESOURCES_LESS_maybe = 0x13e,
  MSG_SCRAP_OWNER_RESOURCES_MORE_maybe = 0x13f,
  MSG_SCRAP_OWNER_maybe = 0x140,
  MSG_SCRAP_OWNER_RESOURCES_maybe = 0x142,
  MSG_INVADE_STARBASE_maybe = 0x135,
  MSG_TRANSFER_BAD_PLAYER_maybe = 0x148,
  MSG_TRANSFER_COLONISTS_maybe = 0x149,
  MSG_TRANSFER_NO_ROOM_maybe = 0x14a,
  MSG_TRANSFER_REFUSED_maybe = 0x14b,
  MSG_TRANSFER_PEACE_maybe = 0x14c,
  MSG_TRANSFERRED_maybe = 0x14d,
  MSG_RECEIVED_FLEET_maybe = 0x14e,
  MSG_CANT_UNLOAD_COLONISTS_FLEET_maybe = 0x155,
  MSG_CANT_UNLOAD_COLONISTS_POINT_maybe = 0x165,
  MSG_NO_MINEFIELD_ROOM_maybe = 0x17e,
  MSG_SUBJECT_SALVAGE_maybe = 0xfffa,
  PLAYER_REF_FLAG = 0x30                        /* or-ed into a player index to name him in a message argument */
};

/* The frame slots whose address a callee gets, and the ones that the original reads before it writes on some path (the value left by
 * an earlier fleet or call): they stay at the original BP offsets so that those reads see the same bytes. */
#define WP_COPY BP_LOCAL(struct Waypoint, -0x6a)   /* the first waypoint of the fleet being handled (30:0x183a gets its address) */
#define PLANET_HERE BP_LOCAL(struct Planet, -0xc2) /* the planet the fleet is at (7:0x0358 fills it) */
#define DESIGN_COPY BP_LOCAL(struct ShipDesign, -0x150)
#define RELATION_OUT L16(-0x30)                    /* 7:0x32a6 gets its address */
#define LOCATION_A L16(-0x4a)                      /* the first location word of the cargo messages */
#define LAST_REMOVED L32(-0x40)                    /* what the last cargo move took from the target */
#define PASS_OUT L16(-0x2e)                        /* 30:0x6112 gets its address */
#define SALVAGE_OUT L32(-0x3c)                     /* 30:0x183a gets its address */
#define MINING_OUT L16(-0x18)                      /* 5:0x3a72 gets its address */
#define BP_NEAR(off) ((uint16_t)(rt_bp + (off)))

/* The five cargo order words of a waypoint (Waypoint.fld_08 on). */
static inline uint16_t waypoint_order(const struct Waypoint *w, uint16_t k)
{
  uint16_t v;
  memcpy(&v, (const uint8_t *)w + FLD(struct Waypoint, fld_08) + 2 * k, sizeof v);
  return v;
}
static inline void set_waypoint_order(struct Waypoint *w, uint16_t k, uint16_t v)
{
  memcpy((uint8_t *)w + FLD(struct Waypoint, fld_08) + 2 * k, &v, sizeof v);
}
/* The cargo of a fleet: the minerals, the colonists and the fuel are five consecutive dwords (Fleet.cargo[4], Fleet.fuel). */
static inline uint32_t fleet_cargo(uint16_t seg, uint16_t off, uint16_t k) { return rd32(seg, (uint16_t)(off + FLD(struct Fleet, cargo) + 4 * k)); }
/* The owner that a fleet id names (bits 9 to 12). */
static inline uint16_t fleet_id_owner(uint16_t id) { return (uint16_t)((id >> 9) & 0xf); }
/* The three mineral words of a salvage object (+8, +10 and +12). */
static inline int16_t object_mineral(uint32_t object, uint16_t k) { return (int16_t)rd16(FP_SEG(object), (uint16_t)(FP_OFF(object) + 8 + 2 * k)); }
/* The costs of a design: the resource cost at +0x2a and the three mineral costs from +0x2c (struct ShipDesign has them as padding). */
static inline uint16_t design_resource_cost(const struct ShipDesign *d)
{
  uint16_t v;
  memcpy(&v, d->unknown_2A, sizeof v);
  return v;
}
static inline uint16_t design_mineral_cost(const struct ShipDesign *d, uint16_t mineral)
{
  uint16_t v;
  memcpy(&v, d->unknown_2A + 2 + 2 * mineral, sizeof v);
  return v;
}
static inline int32_t idiv32(int32_t n, int32_t d)
{
  if (d == 0 || (n == INT32_MIN && d == -1)) divide_error(6);
  return n / d;
}

/* ---- the game's calls ---- */
static bool read_planet_here(uint16_t id) { return (uint16_t)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, id, BP_NEAR(-0xc2)) != 0; }
/* How the fleet stands with the owner of the target: 7:0x32a6(fleet, 0, &answer) leaves 0 (nothing allowed) or a nonzero right. */
static void check_rights(uint16_t fseg, uint16_t foff) { (void)CALL_ORIG(7, 0x32a6, FAR_CDECL, 5, foff, fseg, 0, 0, BP_NEAR(-0x30)); }
static uint32_t fleet_max_capacity(uint16_t seg, uint16_t off, uint16_t which) { return (uint32_t)CALL_ORIG(10, 0x292a, FAR_CDECL, 3, off, seg, which); }
static int32_t fleet_cargo_room(uint16_t seg, uint16_t off) { return (int32_t)CALL_ORIG(10, 0x3a80, FAR_CDECL, 2, off, seg); }
static int32_t fleet_fuel_room(uint16_t seg, uint16_t off) { return (int32_t)CALL_ORIG(10, 0x3ac6, FAR_CDECL, 2, off, seg); }
/* 10:0x3aec(holder kind, holder id, cargo, delta, 0): add delta of a cargo to a planet (kind 1), a fleet (2), ... and return what was moved. */
static int32_t move_cargo(uint16_t holder_kind, uint16_t holder_id, uint16_t cargo, int32_t delta)
{
  return (int32_t)CALL_ORIG(10, 0x3aec, FAR_CDECL, 6, holder_kind, holder_id, cargo, (uint16_t)delta, (uint16_t)((uint32_t)delta >> 16), 0);
}
static uint16_t fleet_name_maybe(uint16_t seg, uint16_t off) { return (uint16_t)CALL_ORIG(7, 0x1bca, FAR_CDECL, 2, off, seg); }
static void delete_order_messages(uint16_t owner, uint16_t fleet_id)
{
  const uint16_t a[3] = {owner, MSG_ORDER_DONE_maybe, (uint16_t)(fleet_id | FLEET_SUBJECT_FLAG)};
  (void)far_call(6, 0x80fa, a, 3);
}
/* A message with one value to the owner of a fleet about the fleet: subject the fleet, first value its id. */
static void order_message2(const struct Fleet *f, uint16_t id, uint16_t v1) { send_message2(f->owner, id, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, v1); }

/* The end of an order that is done or cannot be done: when the fleet has only its own position as a waypoint and still holds a task,
 * the earlier "done" messages go and a new one is sent; the task is cleared. */
static void finish_order(uint16_t fseg, uint16_t foff)
{
  struct Fleet *f = FLEET(fseg, foff);
  struct Waypoint *first = fleet_waypoint(fseg, foff, 0);
  if (f->waypoint_count == 1 && (f->flags & FLEET_FLAG_EMPTY) == 0 && waypoint_task(first) != TASK_NONE) {
    delete_order_messages(f->owner, f->id);
    order_message2(f, MSG_ORDER_DONE_maybe, 0);
  }
  first->flags = (uint16_t)(first->flags & ~0x000f);
}

/* ---- transport orders ---- */

/* The state of the transport order of one fleet. The target is a planet (the one the fleet is at), a fleet, a point or a salvage object;
 * the cargo moves with the target, or with the planet when the target is a fleet that works for the planet. */
struct Transport {
  uint16_t fseg, foff; /* the fleet */
  uint16_t phase;
  bool loading;        /* an even phase loads, an odd one unloads */
  uint16_t kind;       /* what the first waypoint points at */
  uint16_t tseg, toff; /* the target fleet */
  uint32_t object;     /* the target salvage object */
  uint16_t loc_b;      /* the second location word of the messages: the target id, or a coordinate */
  bool foreign;        /* the target belongs to somebody else and answers to 7:0x32a6 */
  bool planet_instead; /* the target fleet has no cargo room: the planet takes its place */
  uint16_t via_planet; /* 1: the target fleet mines the planet we are at, 2: it was found there; the planet takes the cargo */
  uint16_t pass;       /* 0, then 1 when a dunnage order waits for the second pass, 2 in that pass */
  bool satisfied;      /* every order is done */
  bool waits_done;     /* no "wait for percent" order is still unmet */
  bool dunnage_fuel;   /* a fuel dunnage order was found */
  uint32_t dunnage_fuel_taken;
};

/* Find the target of the first waypoint. False when the order is over without a message (the caller then finishes the order). */
static bool resolve_transport_target(struct Transport *t, bool planet_found)
{
  struct Fleet *f = FLEET(t->fseg, t->foff);
  switch (t->kind) {
    case TARGET_PLANET:
      LOCATION_A = NO_PLAYER;
      RELATION_OUT = f->owner == PLANET_HERE.owner ? 3 : 0;
      if (RELATION_OUT != 0) break;
      check_rights(t->fseg, t->foff);
      if (RELATION_OUT == 1) RELATION_OUT = 0;
      if (RELATION_OUT != 0) t->foreign = true;
      if (RELATION_OUT != 0 || PLANET_HERE.owner != OWNER_NONE) break;
      /* an unowned planet and no rights: a mining fleet of the player's in orbit stands in for the planet */
      for (uint16_t index = 0; (int16_t)index < (int16_t)g_fleet_count; index++) {
        const uint32_t other = fleet_ptr_at((uint16_t)(index << 2));
        if (other == 0) break;
        const struct Fleet *o = FLEET(FP_SEG(other), FP_OFF(other));
        if ((o->flags & FLEET_FLAG_EMPTY) == 0 && o->owner == f->owner && o->planet_id == f->planet_id &&
            (FP_SEG(other) != t->fseg || FP_OFF(other) != t->foff) && (o->flags & FLEET_FLAG_AT_PLANET_maybe) != 0) {
          const uint16_t a[2] = {FP_OFF(other), FP_SEG(other)};
          if ((int32_t)far_call(16, 0x193e, a, 2) > 0) {
            t->via_planet = 2;
            RELATION_OUT = 1;
            WP_COPY.flags = (uint16_t)((WP_COPY.flags & 0xf2ff) | (TARGET_FLEET << 8));
            WP_COPY.target_id = o->id;
            LOCATION_A = NO_PLAYER;
            t->loc_b = (uint16_t)(o->id | FLEET_SUBJECT_FLAG);
            t->tseg = FP_SEG(other);
            t->toff = FP_OFF(other);
            break;
          }
        }
      }
      break;
    case TARGET_FLEET: {
      const uint32_t followed = fleet_by_id(WP_COPY.target_id);
      t->tseg = FP_SEG(followed);
      t->toff = FP_OFF(followed);
      const struct Fleet *target = FLEET(t->tseg, t->toff);
      t->loc_b = (uint16_t)(t->loc_b | FLEET_SUBJECT_FLAG);
      LOCATION_A = NO_PLAYER;
      RELATION_OUT = target->owner == f->owner ? 1 : 0;
      if (RELATION_OUT == 0) {
        check_rights(t->fseg, t->foff);
        if (RELATION_OUT != 0) t->foreign = true;
        break;
      }
      if ((target->flags & FLEET_FLAG_AT_PLANET_maybe) == 0 || !planet_found) break;
      const uint16_t a[2] = {t->toff, t->tseg};
      if ((int32_t)far_call(16, 0x193e, a, 2) > 0 && (PLANET_HERE.owner == OWNER_NONE || f->owner == PLANET_HERE.owner)) {
        t->via_planet = 1; /* the target fleet mines this planet: its minerals are the planet's */
        LOCATION_A = NO_PLAYER;
        t->loc_b = (uint16_t)(target->id | FLEET_SUBJECT_FLAG);
        break;
      }
      if (fleet_max_capacity(t->tseg, t->toff, CARGO_CAPACITY) != 0) break;
      if (f->owner == PLANET_HERE.owner) {
        t->planet_instead = true; /* the target fleet has no cargo room: our own planet takes its place */
        LOCATION_A = NO_PLAYER;
        t->loc_b = (uint16_t)(target->id | FLEET_SUBJECT_FLAG);
      }
      break;
    }
    case TARGET_POINT:
      LOCATION_A = WP_COPY.x;
      t->loc_b = WP_COPY.y;
      break;
    case TARGET_OBJECT:
      t->object = map_object_by_id(WP_COPY.target_id);
      LOCATION_A = 0xfffe;
      break;
    default:
      break;
  }
  t->kind = waypoint_target_kind(&WP_COPY); /* the waypoint may just have been re-aimed at a fleet */
  t->loading = (uint8_t)(t->phase + 1) & 1;
  t->dunnage_fuel = false;
  if (t->kind == TARGET_OBJECT) {
    if (t->object == 0) return false;
    const struct MapObject *object = FAR_REC(struct MapObject, FP_SEG(t->object), FP_OFF(t->object));
    if (map_object_kind(object->owner_kind_bits) != SALVAGE_TYPE_maybe) {
      send_message2(f->owner, MSG_NOT_SALVAGE_maybe, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, (uint16_t)(object->id_kind >> 13));
      return false;
    }
  }
  return true;
}

/* How much of a cargo the target holds, for the loading orders. */
static uint32_t cargo_available(const struct Transport *t, uint16_t k)
{
  switch (t->kind) {
    case TARGET_OBJECT:
      if (k < CARGO_MINERALS) return (uint32_t)(int32_t)object_mineral(t->object, k);
      return 0;
    case TARGET_FLEET:
      if ((t->via_planet == 0 && !t->planet_instead) || k == CARGO_FUEL) return fleet_cargo(t->tseg, t->toff, k);
      __attribute__((fallthrough)); /* the planet holds the cargo */
    case TARGET_PLANET:
      if (k < CARGO_FUEL) return PLANET_HERE.surface_amounts_maybe[k];
      return 0;
    default:
      return 0;
  }
}

/* A percentage order works on the fleet's capacity (cut to 2000000); the arithmetic keeps the product inside 32 bits. */
static uint32_t percent_of_capacity(uint16_t fseg, uint16_t foff, uint16_t k, uint16_t percent)
{
  int32_t capacity = (int32_t)fleet_max_capacity(fseg, foff, k == CARGO_FUEL ? FUEL_CAPACITY : CARGO_CAPACITY);
  if (capacity > CAPACITY_CAP) capacity = CAPACITY_CAP;
  if (capacity >= 0x10000) return (uint32_t)((int32_t)percent * idiv32(capacity, 100));
  return ((uint32_t)percent * (uint32_t)capacity) / 100u;
}

/* The message about a refused load in the last phase: the target kind decides which. */
static uint16_t cant_load_message(uint16_t kind)
{
  /* only planet, fleet and point targets reach this: every other kind has full rights */
  return kind == TARGET_PLANET ? MSG_CANT_LOAD_PLANET_maybe : kind == TARGET_FLEET ? MSG_CANT_LOAD_FLEET_maybe : MSG_CANT_LOAD_POINT_maybe;
}

/* Load one cargo from the target into the fleet. True when the order ends (the last phase could not load). */
static bool load_cargo(struct Transport *t, uint16_t k, uint16_t type, uint32_t amount, uint32_t available)
{
  struct Fleet *f = FLEET(t->fseg, t->foff);
  if (!t->loading) return false;
  const int32_t room = k == CARGO_FUEL ? fleet_fuel_room(t->fseg, t->foff) : fleet_cargo_room(t->fseg, t->foff);
  if (room <= (int32_t)amount) amount = (uint32_t)room;
  if (RELATION_OUT == 0 || t->kind == TARGET_POINT) {
    if (k == CARGO_FUEL && t->dunnage_fuel) return false;
    if (t->phase == PHASE_LAST) {
      send_message2(f->owner, cant_load_message(t->kind), (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, k);
      return true;
    }
    t->satisfied = false;
    return false;
  }
  if (t->foreign && (k == CARGO_COLONISTS || k == CARGO_FUEL)) t->satisfied = true;
  if (amount == 0) {
    if (type == ORDER_WAIT_FOR_PERCENT) {
      if (k == CARGO_FUEL)
        t->satisfied = false;
      else
        t->waits_done = false;
    }
    return false;
  }
  const uint16_t subject = (uint16_t)(f->id | FLEET_SUBJECT_FLAG);
  const uint16_t message = k == CARGO_COLONISTS ? MSG_LOADED_maybe + 1 : MSG_LOADED_maybe;
  int32_t moved = (int32_t)available <= (int32_t)amount ? (int32_t)available : (int32_t)amount;
  LAST_REMOVED = (uint32_t)move_cargo(t->kind, WP_COPY.target_id, k, -moved);
  if (LAST_REMOVED != 0) {
    moved = move_cargo(TARGET_FLEET, f->id, k, -(int32_t)LAST_REMOVED);
    if (moved != 0) {
      if (t->kind == TARGET_FLEET && FLEET(t->tseg, t->toff)->owner != f->owner)
        send_message(f->owner, MSG_LOADED_FOREIGN_maybe, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k,
                     (uint16_t)(t->loc_b & 0x7fff), 0, 0);
      else
        send_message(f->owner, message, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k, LOCATION_A, t->loc_b, 0);
    }
  }
  if (t->planet_instead || t->via_planet != 0) {
    const int32_t shortfall = (int32_t)(LAST_REMOVED + amount);
    if (shortfall != 0) {
      LAST_REMOVED = (uint32_t)move_cargo(TARGET_PLANET, PLANET_HERE.id, k, -shortfall);
      if (LAST_REMOVED != 0) {
        moved = move_cargo(TARGET_FLEET, f->id, k, -(int32_t)LAST_REMOVED);
        if (t->via_planet != 0)
          send_message(f->owner, MSG_LOADED_VIA_PLANET_maybe, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k,
                       FLEET(t->tseg, t->toff)->id, PLANET_HERE.id, 0);
        else
          send_message(f->owner, message, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k, LOCATION_A, PLANET_HERE.id, 0);
      } else {
        moved = 0;
      }
    }
  }
  if (moved != (int32_t)amount && (type == ORDER_WAIT_FOR_PERCENT || type == ORDER_FILL_TO_PERCENT)) t->satisfied = false;
  if (moved != 0 && type == ORDER_LOAD_DUNNAGE && k == CARGO_FUEL) t->dunnage_fuel_taken = (uint32_t)moved;
  return false;
}

/* Unload one cargo from the fleet into the target. True when the order ends (a message says why). */
static bool unload_cargo(struct Transport *t, uint16_t k, uint32_t amount)
{
  struct Fleet *f = FLEET(t->fseg, t->foff);
  const uint16_t subject = (uint16_t)(f->id | FLEET_SUBJECT_FLAG);
  if (t->loading) return false;
  bool invaded = false;
  if (k == CARGO_COLONISTS && t->kind == TARGET_PLANET && f->owner != PLANET_HERE.owner) {
    /* colonists on somebody else's planet (or an unowned one) are an invasion */
    uint16_t refusal = 0;
    if (PLANET_HERE.owner == OWNER_NONE && (PLANET_HERE.flags & PLANET_FLAG_OPEN_maybe) == 0)
      refusal = MSG_INVADE_NOT_OPEN_maybe;
    else if (player_race(f->owner) == PLAYER_RACE_NO_INVASION_maybe)
      refusal = MSG_INVADE_RACE_maybe;
    else if ((PLANET_HERE.flags & PLANET_FLAG_HAS_STARBASE) != 0)
      refusal = MSG_INVADE_STARBASE_maybe;
    if (refusal != 0) {
      send_message2(f->owner, refusal, subject, f->id, PLANET_HERE.id);
      return true;
    }
    (void)CALL_ORIG(23, 0x2fda, FAR_CDECL, 6, t->foff, t->fseg, BP_NEAR(-0xc2), MEM_SEL_DGROUP, (uint16_t)amount, (uint16_t)(amount >> 16));
    invaded = true;
  }
  if (!invaded) {
    if (k == CARGO_COLONISTS && t->kind == TARGET_FLEET && f->owner != fleet_id_owner(WP_COPY.target_id)) {
      send_message2(f->owner, MSG_CANT_UNLOAD_COLONISTS_FLEET_maybe, subject, f->id, PLANET_HERE.id);
      return true;
    }
    if (t->kind == TARGET_FLEET && player_relation(FLEET(t->tseg, t->toff)->owner, f->owner) == RELATION_ENEMY_maybe) {
      amount = 0;
    } else {
      if (k == CARGO_COLONISTS && t->kind == TARGET_POINT) {
        send_message2(f->owner, MSG_CANT_UNLOAD_COLONISTS_POINT_maybe, subject, f->id, 0);
        return true;
      }
      if (amount != 0) {
        const uint16_t message = k == CARGO_COLONISTS ? MSG_UNLOADED_maybe + 1 : MSG_UNLOADED_maybe;
        if (t->planet_instead && k != CARGO_FUEL) {
          const int32_t moved = move_cargo(TARGET_PLANET, PLANET_HERE.id, k, (int32_t)amount);
          send_message(f->owner, message, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k, LOCATION_A, PLANET_HERE.id, 0);
        } else {
          const int32_t moved = move_cargo(t->kind, WP_COPY.target_id, k, (int32_t)amount);
          if (moved > 0)
            send_message(f->owner, message, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), k, LOCATION_A, t->loc_b, 0);
          amount = (uint32_t)moved;
        }
      }
    }
  }
  if (amount != 0) (void)move_cargo(TARGET_FLEET, f->id, k, -(int32_t)amount);
  set_waypoint_order(&WP_COPY, k, (uint16_t)(waypoint_order(&WP_COPY, k) & ORDER_AMOUNT_MASK)); /* the order is used up */
  return false;
}

/* One cargo slot of a transport order. True when the order ends. */
static bool transport_slot(struct Transport *t, uint16_t k)
{
  const uint16_t order = waypoint_order(&WP_COPY, k);
  const uint16_t type = (uint16_t)(order >> ORDER_TYPE_SHIFT);
  const uint16_t amount_field = (uint16_t)(order & ORDER_AMOUNT_MASK);
  if (type == 0) return false;
  if (t->pass == 2 && type != ORDER_LOAD_DUNNAGE) return false;
  if (k == CARGO_FUEL && t->kind != TARGET_FLEET && t->kind != TARGET_POINT) return false;
  if (k >= CARGO_COLONISTS && t->kind == TARGET_OBJECT) return false;
  const uint32_t available = cargo_available(t, k);
  const uint32_t carried = fleet_cargo(t->fseg, t->foff, k);
  uint32_t wanted;
  switch (type) {
    case ORDER_LOAD_ALL:
      if (!t->loading) return false;
      wanted = available;
      break;
    case ORDER_UNLOAD_ALL:
      if (t->loading) return false;
      wanted = carried;
      break;
    case ORDER_LOAD_EXACT:
      if (!t->loading) return false;
      wanted = amount_field;
      break;
    case ORDER_UNLOAD_EXACT:
      if (t->loading) return false;
      wanted = amount_field;
      break;
    case ORDER_FILL_TO_PERCENT:
    case ORDER_WAIT_FOR_PERCENT:
      if (!t->loading) return false;
      wanted = percent_of_capacity(t->fseg, t->foff, k, amount_field);
      break;
    case ORDER_LOAD_DUNNAGE:
      wanted = available;
      break;
    case ORDER_SET_AMOUNT:
    case ORDER_SET_WAYPOINT:
      wanted = amount_field;
      break;
    default:
      return false;
  }
  switch (type) {
    case ORDER_LOAD_ALL:
    case ORDER_LOAD_EXACT:
    case ORDER_FILL_TO_PERCENT:
    case ORDER_WAIT_FOR_PERCENT:
      return load_cargo(t, k, type, wanted, available);
    case ORDER_UNLOAD_ALL:
    case ORDER_UNLOAD_EXACT:
      return unload_cargo(t, k, carried <= wanted ? carried : wanted);
    case ORDER_LOAD_DUNNAGE:
      if (k == CARGO_FUEL) {
        t->dunnage_fuel_taken = 0;
        t->dunnage_fuel = true;
        return false;
      }
      if (!t->loading) return false;
      if ((int16_t)t->pass < 2) {
        t->pass = 1; /* the minerals wait for the second pass */
        return false;
      }
      if (available == 0) return false;
      return load_cargo(t, k, type, available, available);
    case ORDER_SET_AMOUNT: {
      const int32_t difference = (int32_t)(wanted - carried); /* what the fleet still needs, or minus what it has too much */
      if (difference < 0) {
        if (t->loading) return false;
        return unload_cargo(t, k, (uint32_t)-difference);
      }
      if (!t->loading) return false;
      if ((int32_t)available < difference) {
        t->satisfied = false;
        if (t->phase == PHASE_LAST) {
          const struct Fleet *f = FLEET(t->fseg, t->foff);
          send_message(f->owner, (uint16_t)(MSG_NOT_ENOUGH_FOR_SET_AMOUNT_maybe + (k == CARGO_COLONISTS ? 1 : 0)), (uint16_t)(f->id | FLEET_SUBJECT_FLAG),
                       f->id, k, amount_field, 0, LOCATION_A, t->loc_b, 0);
        }
      }
      return load_cargo(t, k, type, (uint32_t)difference, available);
    }
    case ORDER_SET_WAYPOINT: {
      const int32_t difference = (int32_t)(available - wanted); /* what the target holds too much, or minus what it lacks */
      if (difference >= 0) {
        if (!t->loading) return false;
        return load_cargo(t, k, type, (uint32_t)difference, available);
      }
      if (t->loading) return false;
      const uint32_t lack = (uint32_t)-difference;
      return unload_cargo(t, k, (int32_t)lack > (int32_t)carried ? carried : lack);
    }
    default:
      return false;
  }
}

/* The fuel the fleet needs for its next leg (10:0x6312 with the warp of the next waypoint and any distance). */
static int32_t fuel_to_next_waypoint(uint16_t fseg, uint16_t foff) { return (int32_t)fleet_fuel_figure(MK_FP(fseg, foff), 0xffff, 0xffffffffu, 0); }

/* A fuel dunnage order gives the target the fuel the fleet does not need for its next leg (all of it when there is no next leg).
 * False when the fleet is short of fuel for the leg: the messages say so and the step ends there. */
static bool give_surplus_fuel(struct Transport *t)
{
  struct Fleet *f = FLEET(t->fseg, t->foff);
  const uint16_t subject = (uint16_t)(f->id | FLEET_SUBJECT_FLAG);
  uint32_t excess;
  int32_t moved;
  if ((int16_t)f->waypoint_count <= 1) {
    excess = f->fuel;
  } else {
    int32_t need = fuel_to_next_waypoint(t->fseg, t->foff);
    if ((int32_t)f->fuel < need) {
      t->satisfied = false;
      if (t->dunnage_fuel_taken != 0)
        send_message(f->owner, MSG_LOADED_maybe, subject, f->id, (uint16_t)t->dunnage_fuel_taken, (uint16_t)(t->dunnage_fuel_taken >> 16), CARGO_FUEL,
                     LOCATION_A, t->loc_b, 0);
      if (t->phase == PHASE_LAST && RELATION_OUT == 0) {
        send_message(f->owner, MSG_NO_FUEL_LEFT_maybe, subject, f->id, LOCATION_A, t->loc_b, 0, 0, 0, 0);
      } else if (t->phase == 2) {
        const int32_t capacity = (int32_t)fleet_max_capacity(t->fseg, t->foff, FUEL_CAPACITY);
        if (capacity < need) {
          send_message(f->owner, MSG_FUEL_SHORT_FOR_TANK_maybe, subject, f->id, (uint16_t)capacity, (uint16_t)((uint32_t)capacity >> 16), (uint16_t)need,
                       (uint16_t)((uint32_t)need >> 16), 0, 0);
        } else {
          const uint32_t shortfall = (uint32_t)need - f->fuel;
          if (t->via_planet == 2) t->loc_b = PLANET_HERE.id;
          send_message(f->owner, MSG_FUEL_SHORT_maybe, subject, LOCATION_A, t->loc_b, f->id, (uint16_t)shortfall, (uint16_t)(shortfall >> 16), 0, 0);
        }
      }
      return false;
    }
    if ((int32_t)f->fuel == need) return true;
    const uint32_t keep = f->fuel;
    do {
      f->fuel = (uint32_t)need;
      need = fuel_to_next_waypoint(t->fseg, t->foff);
    } while ((int32_t)f->fuel > need);
    excess = keep - (uint32_t)need;
    f->fuel = keep;
  }
  if (excess != 0) {
    LAST_REMOVED = (uint32_t)move_cargo(t->kind, WP_COPY.target_id, CARGO_FUEL, (int32_t)excess);
    moved = 0;
    if (LAST_REMOVED != 0) moved = move_cargo(TARGET_FLEET, f->id, CARGO_FUEL, -(int32_t)LAST_REMOVED);
  } else {
    moved = 0;
  }
  if (LAST_REMOVED != 0) {
    moved += (int32_t)t->dunnage_fuel_taken;
    if (moved > 0)
      send_message(f->owner, MSG_LOADED_maybe, subject, f->id, (uint16_t)moved, (uint16_t)((uint32_t)moved >> 16), CARGO_FUEL, LOCATION_A, t->loc_b, 0);
    else if (moved < 0)
      send_message(f->owner, MSG_UNLOADED_maybe, subject, f->id, (uint16_t)-moved, (uint16_t)((uint32_t)-moved >> 16), CARGO_FUEL, LOCATION_A,
                   t->loc_b, 0);
  }
  return true;
}

/* The transport order of a fleet: the cargo slots in order (twice when dunnage waits), the fuel dunnage, and then either the order is
 * over (the last phase and every order met) or the waypoint, possibly re-aimed, goes back into the fleet. */
static void carry_out_transport(uint16_t phase, uint16_t fseg, uint16_t foff)
{
  struct Fleet *f = FLEET(fseg, foff);
  struct Transport t;
  memset(&t, 0, sizeof t);
  t.fseg = fseg;
  t.foff = foff;
  t.phase = phase;
  t.kind = waypoint_target_kind(&WP_COPY);
  t.satisfied = true;
  t.waits_done = true;
  RELATION_OUT = 1;
  const bool planet_found = f->planet_id != FLEET_IN_DEEP_SPACE && read_planet_here(f->planet_id);
  t.loc_b = WP_COPY.target_id;
  if (!resolve_transport_target(&t, planet_found)) {
    finish_order(fseg, foff);
    return;
  }
  for (;;) {
    for (uint16_t k = 0; k < CARGO_ORDERS; k++) {
      if (transport_slot(&t, k)) {
        finish_order(fseg, foff);
        return;
      }
    }
    if (t.dunnage_fuel && t.loading && t.pass != 1 && give_surplus_fuel(&t) && t.satisfied && t.waits_done) set_waypoint_order(&WP_COPY, CARGO_FUEL, 0);
    if (!t.waits_done && fleet_cargo_room(fseg, foff) > 0) t.satisfied = false;
    if (t.satisfied && t.pass == 1 && (fleet_cargo_room(fseg, foff) > 0 || t.dunnage_fuel)) {
      t.pass = 2; /* the minerals that waited for the end load now */
      continue;
    }
    break;
  }
  if (t.satisfied && t.loading) {
    finish_order(fseg, foff);
    return;
  }
  if (t.via_planet == 2) {
    WP_COPY.target_id = PLANET_HERE.id;
    WP_COPY.flags = (uint16_t)((WP_COPY.flags & 0xf1ff) | (TARGET_PLANET << 8));
  }
  *fleet_waypoint(fseg, foff, 0) = WP_COPY;
}

/* ---- remote mining, route, merge, transfer ---- */

/* Remote mining (phase 3): the fleet in orbit of an unowned planet mines it with its mining rate (16:0x193e). */
static void remote_mining_order(uint16_t phase, uint16_t fseg, uint16_t foff)
{
  struct Fleet *f = FLEET(fseg, foff);
  if (phase != PHASE_REMOTE_MINING || (f->flags & FLEET_FLAG_AT_PLANET_maybe) == 0) return;
  if (f->planet_id == FLEET_IN_DEEP_SPACE) {
    order_message2(f, MSG_REMOTE_MINING_IN_SPACE_maybe, 0);
    finish_order(fseg, foff);
    return;
  }
  const uint32_t planet = planet_by_id(f->planet_id);
  if (planet == 0) {
    finish_order(fseg, foff);
    return;
  }
  const struct Planet *p = PLANET(FP_SEG(planet), FP_OFF(planet));
  const uint16_t rate_args[2] = {foff, fseg};
  const uint32_t rate = far_call(16, 0x193e, rate_args, 2);
  if (rate == 0) {
    order_message2(f, MSG_REMOTE_MINING_NO_RATE_maybe, p->id);
    finish_order(fseg, foff);
    return;
  }
  if (p->owner != OWNER_NONE) {
    if (player_race(f->owner) == PLAYER_RACE_NO_INVASION_maybe) return;
    order_message2(f, MSG_REMOTE_MINING_INHABITED_maybe, p->id);
    finish_order(fseg, foff);
    return;
  }
  const uint16_t mine_args[6] = {FP_OFF(planet), FP_SEG(planet), BP_NEAR(-0x18), (uint16_t)rate, (uint16_t)(rate >> 16), 1};
  (void)far_call(5, 0x3a72, mine_args, 6);
}

/* Route: a fleet that waits at a planet with a route target is sent on to it (16:0x13f8). In the last phase another fleet's route
 * may turn into a merge (16:0x17c2 sets the task); True then. */
static bool route_order(uint16_t phase, uint16_t fseg, uint16_t foff)
{
  struct Fleet *f = FLEET(fseg, foff);
  if (f->waypoint_count != 1 || f->planet_id == FLEET_IN_DEEP_SPACE) return false;
  const uint32_t planet = planet_by_id(f->planet_id);
  const struct Planet *p = PLANET(FP_SEG(planet), FP_OFF(planet));
  const uint16_t args[4] = {foff, fseg, FP_OFF(planet), FP_SEG(planet)};
  if (p->owner == f->owner && (p->route_target & PLANET_ROUTE_TARGET_MASK) != 0) {
    (void)far_call(16, 0x13f8, args, 4);
    const uint16_t message = (uint16_t)(MSG_ROUTE_maybe + (waypoint_warp(fleet_waypoint(fseg, foff, 1)) == 0 ? 1 : 0));
    send_message(f->owner, message, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, f->planet_id,
                 (uint16_t)((p->route_target & PLANET_ROUTE_TARGET_MASK) - 1), 0, 0, 0, 0);
    return false;
  }
  if (phase != PHASE_LAST) return false;
  (void)far_call(16, 0x17c2, args, 4);
  WP_COPY = *fleet_waypoint(fseg, foff, 0);
  return waypoint_task(&WP_COPY) == TASK_MERGE;
}

/* Merge (even phases): the fleet joins the fleet its waypoint points at, if it is the same player's. */
static void merge_order(uint16_t phase, uint16_t fseg, uint16_t foff)
{
  struct Fleet *f = FLEET(fseg, foff);
  if ((phase & 1) != 0) return;
  if (waypoint_target_kind(&WP_COPY) == TARGET_FLEET) {
    const uint32_t target = fleet_by_id(WP_COPY.target_id);
    const struct Fleet *t = FLEET(FP_SEG(target), FP_OFF(target));
    if (target != 0 && (t->flags & FLEET_FLAG_EMPTY) == 0) {
      if (FP_SEG(target) == fseg && FP_OFF(target) == foff) {
        finish_order(fseg, foff);
        return;
      }
      if (t->owner != f->owner) {
        order_message2(f, MSG_MERGE_OTHER_PLAYER_maybe, 0);
      } else {
        send_message2(f->owner, MSG_MERGED_maybe, (uint16_t)(t->id | FLEET_SUBJECT_FLAG), fleet_name_maybe(fseg, foff), t->id);
        delete_order_messages(f->owner, f->id);
        (void)CALL_ORIG(10, 0x78b6, FAR_CDECL, 5, FP_OFF(target), FP_SEG(target), foff, fseg, 1);
      }
      finish_order(fseg, foff);
      return;
    }
  }
  order_message2(f, MSG_MERGE_NO_TARGET_maybe, 0);
  finish_order(fseg, foff);
}

/* A message that a transfer of the fleet fails: to its owner, and to the player who would have got it. */
static void transfer_refused_no_room(const struct Fleet *f, uint16_t recipient)
{
  send_message2(f->owner, MSG_TRANSFER_NO_ROOM_maybe, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, (uint16_t)(recipient | PLAYER_REF_FLAG));
  send_message2(recipient, MSG_TRANSFER_REFUSED_maybe, NO_PLAYER, (uint16_t)(f->owner | PLAYER_REF_FLAG), 0);
}

/* Transfer (last phase): the whole fleet goes to another player (counted without the fleet's owner) who is human, not at war with the
 * owner and has room: the designs the new owner lacks are copied into his free slots, and a new fleet is made at the same place. */
static void transfer_order(uint16_t fseg, uint16_t foff, uint16_t *fleet_index)
{
  struct Fleet *f = FLEET(fseg, foff);
  uint16_t recipient = waypoint_order(&WP_COPY, 0);
  if ((int16_t)f->owner <= (int16_t)recipient) recipient++;
  const uint16_t subject = (uint16_t)(f->id | FLEET_SUBJECT_FLAG);
  if ((int16_t)recipient < 0 || (int16_t)g_player_count <= (int16_t)recipient || (*player_orders_flags(recipient) & PLAYER_NO_TRANSFER_maybe) != 0) {
    order_message2(f, MSG_TRANSFER_BAD_PLAYER_maybe, 0);
    finish_order(fseg, foff);
    return;
  }
  if ((player_row(recipient)->status_flags & PLAYER_STATUS_COMPUTER_maybe) != 0 || player_relation(recipient, f->owner) == RELATION_ENEMY_maybe) {
    send_message2(f->owner, MSG_TRANSFER_PEACE_maybe, subject, (uint16_t)(recipient | PLAYER_REF_FLAG), 0);
    finish_order(fseg, foff);
    return;
  }
  if ((int32_t)fleet_cargo(fseg, foff, CARGO_COLONISTS) > 0) {
    order_message2(f, MSG_TRANSFER_COLONISTS_maybe, 0);
    finish_order(fseg, foff);
    return;
  }
  /* the design of each slot the fleet uses, in the new owner's list: the same design, or a free slot of his */
  uint16_t remap[SHIP_DESIGNS_PER_PLAYER];
  uint16_t cursor = 0xffff;
  const uint32_t from_designs = player_designs(f->owner);
  const uint32_t to_designs = player_designs(recipient);
  for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
    if (f->ship_count[slot] == 0) {
      remap[slot] = 0xffff;
      continue;
    }
    uint16_t found = (uint16_t)CALL_ORIG(7, 0x4dcc, FAR_CDECL, 3, (uint16_t)(FP_OFF(from_designs) + (uint16_t)(slot * sizeof(struct ShipDesign))),
                                         FP_SEG(from_designs), recipient);
    if (found == 0xffff) {
      do {
        cursor++;
      } while (cursor < SHIP_DESIGNS_PER_PLAYER && (design_of(to_designs, cursor)->flags & DESIGN_FLAG_FREE) == 0);
      if (cursor >= SHIP_DESIGNS_PER_PLAYER) {
        transfer_refused_no_room(f, recipient);
        finish_order(fseg, foff);
        return;
      }
      found = cursor;
    }
    remap[slot] = found;
  }
  uint32_t made = 0;
  if ((player_row(recipient)->fleet_and_sb_counts & PLAYER_FLEET_COUNT_MASK) < FLEETS_PER_PLAYER_MAX_maybe)
    made = (uint32_t)CALL_ORIG(7, 0x1f2e, FAR_CDECL, 2, recipient, f->planet_id); /* a new fleet of his at the same planet */
  if (made == 0) {
    transfer_refused_no_room(f, recipient);
    finish_order(fseg, foff);
    return;
  }
  struct Fleet *nf = FLEET(FP_SEG(made), FP_OFF(made));
  if ((int16_t)f->owner > (int16_t)recipient) (*fleet_index)++;
  nf->xy = f->xy;
  fleet_waypoint(FP_SEG(made), FP_OFF(made), 0)->x = f->x;
  fleet_waypoint(FP_SEG(made), FP_OFF(made), 0)->y = f->y;
  memcpy((uint8_t *)nf + FLD(struct Fleet, cargo), (const uint8_t *)f + FLD(struct Fleet, cargo), 5 * sizeof(uint32_t));
  for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
    if ((int16_t)f->ship_count[slot] <= 0) continue;
    const uint16_t to_slot = remap[slot];
    struct ShipDesign *design = design_of(to_designs, to_slot);
    if ((design->flags & DESIGN_FLAG_FREE) != 0) {
      memcpy(design, design_of(from_designs, slot), sizeof *design);
      design->flags = (uint8_t)((design->flags & ~DESIGN_SLOT_BITS_maybe) | ((to_slot << 2) & DESIGN_SLOT_BITS_maybe));
      design->flags = (uint8_t)(design->flags | DESIGN_FLAG_BIT7_maybe);
      design->fld_7f = 0;
      design->build_progress_maybe = 0;
      player_row(recipient)->design_count++;
    }
    nf->ship_count[to_slot] = (uint16_t)(nf->ship_count[to_slot] + f->ship_count[slot]);
    nf->damage[to_slot] = f->damage[slot];
    design->fld_7f += (uint32_t)(int32_t)(int16_t)f->ship_count[slot];
    design->build_progress_maybe += (uint32_t)(int32_t)(int16_t)f->ship_count[slot];
  }
  f->flags = (uint8_t)(f->flags | FLEET_FLAG_EMPTY);
  send_message2(f->owner, MSG_TRANSFERRED_maybe, (uint16_t)(nf->id | FLEET_SUBJECT_FLAG), fleet_name_maybe(fseg, foff),
                (uint16_t)(recipient | PLAYER_REF_FLAG));
  send_message2(recipient, MSG_RECEIVED_FLEET_maybe, (uint16_t)(nf->id | FLEET_SUBJECT_FLAG), (uint16_t)(f->owner | PLAYER_REF_FLAG),
                fleet_name_maybe(FP_SEG(made), FP_OFF(made)));
  f->flags = (uint8_t)(f->flags | FLEET_FLAG_EMPTY);
}

/* ---- mines ---- */

/* Lay mines (phase 3): a fleet with the order lays the mines it can: into the nearest minefield of its own that it is inside (one
 * below the size limit; the field grows and its centre moves toward the fleet), else into a new field. A race 5 fleet with no task
 * does it when its next waypoint says so. */
static void lay_mines_order(uint16_t phase, uint16_t fseg, uint16_t foff, uint16_t task)
{
  struct Fleet *f = FLEET(fseg, foff);
  if (task == TASK_NONE) {
    if ((int16_t)f->waypoint_count <= 1 || player_race(f->owner) != PLAYER_RACE_MINE_LAYER_maybe ||
        waypoint_task(fleet_waypoint(fseg, foff, 1)) != TASK_LAY_MINES)
      return;
  }
  if (phase != PHASE_REMOTE_MINING) return;
  if ((f->flags & FLEET_FLAG_AT_PLANET_maybe) == 0 && player_race(f->owner) != PLAYER_RACE_MINE_LAYER_maybe) return;
  const uint16_t all_args[4] = {foff, fseg, 0xffff, 0xffff};
  if (far_call(16, 0x1aea, all_args, 4) == 0) {
    order_message2(f, MSG_NO_MINES_maybe, 0);
    finish_order(fseg, foff);
    return;
  }
  if (waypoint_task(&WP_COPY) == TASK_LAY_MINES) {
    struct Waypoint *first = fleet_waypoint(fseg, foff, 0);
    const uint16_t years = waypoint_order(first, 0);
    if (years == 0)
      first->flags = (uint16_t)(first->flags & ~0x000f);
    else if (years != MINE_WAYPOINT_FOREVER)
      set_waypoint_order(first, 0, (uint16_t)(years - 1));
  }
  uint16_t message = MSG_MINES_LAID_maybe; /* once a field has been added to, the original keeps saying so for the later types */
  for (uint16_t type = 0; type < MINE_TYPES; type++) {
    const uint16_t type_args[4] = {foff, fseg, type, 0xffff};
    int32_t laid = (int32_t)far_call(16, 0x1aea, type_args, 4);
    if (laid == 0) continue;
    if ((f->flags & FLEET_FLAG_AT_PLANET_maybe) == 0) laid = idiv32(laid, 2);
    /* the closest minefield of the fleet's own owner and this type that it is inside */
    uint32_t field = 0;
    int32_t field_distance = MINE_DISTANCE_START;
    const uint16_t end = map_objects_end();
    for (uint16_t at = g_map_objects; at < end; at = (uint16_t)(at + sizeof(struct MapObject))) {
      const struct MapObject *o = FAR_REC(struct MapObject, g_map_objects_segment_maybe, at);
      if (map_object_owner(o->owner_kind_bits) != f->owner || map_object_kind(o->owner_kind_bits) != MAPOBJ_KIND_MINEFIELD || o->fld_0c != type) continue;
      const int32_t dx = (int16_t)(uint16_t)(f->x - (uint16_t)o->xy);
      const int32_t dy = (int16_t)(uint16_t)(f->y - (uint16_t)(o->xy >> 16));
      const int32_t distance = (int32_t)((uint32_t)(dx * dx) + (uint32_t)(dy * dy));
      if (distance > (int32_t)minefield_radius_sq(g_map_objects_segment_maybe, at)) continue;
      if (field_distance > distance) {
        field_distance = distance;
        field = MK_FP(g_map_objects_segment_maybe, at);
      }
    }
    if (field != 0 && (int32_t)minefield_radius_sq(FP_SEG(field), FP_OFF(field)) < MINEFIELD_FULL) {
      const uint16_t seg = FP_SEG(field), off = FP_OFF(field);
      const int32_t old_size = (int32_t)minefield_radius_sq(seg, off);
      const int32_t new_size = (int32_t)((uint32_t)old_size + (uint32_t)laid);
      struct MapObject *o = FAR_REC(struct MapObject, seg, off);
      const uint16_t new_x = (uint16_t)idiv32((int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)o->xy * (uint32_t)old_size +
                                                         (uint32_t)(int32_t)(int16_t)f->x * (uint32_t)laid),
                                               new_size);
      const uint16_t new_y = (uint16_t)idiv32((int32_t)((uint32_t)(int32_t)(int16_t)(uint16_t)(o->xy >> 16) * (uint32_t)old_size +
                                                         (uint32_t)(int32_t)(int16_t)f->y * (uint32_t)laid),
                                               new_size);
      o->xy = (uint32_t)new_x | ((uint32_t)new_y << 16);
      set_minefield_radius_sq(seg, off, (uint32_t)new_size);
      message = MSG_MINES_ADDED_maybe;
    } else {
      const uint16_t new_args[2] = {f->owner, 0};
      const uint32_t made = far_call(33, 0x0000, new_args, 2);
      if (made == 0) {
        order_message2(f, MSG_NO_MINEFIELD_ROOM_maybe, 0);
        continue;
      }
      struct MapObject *o = FAR_REC(struct MapObject, FP_SEG(made), FP_OFF(made));
      o->xy = f->xy;
      set_minefield_radius_sq(FP_SEG(made), FP_OFF(made), (uint32_t)laid);
      o->fld_0c = (uint8_t)type;
    }
    send_message(f->owner, message, (uint16_t)(f->id | FLEET_SUBJECT_FLAG), f->id, (uint16_t)laid, (uint16_t)((uint32_t)laid >> 16), 0, 0, 0, 0);
  }
}

/* ---- colonize and scrap ---- */

/* 16:0x244c adds up the parts of the designs into the data segment from 0x27d6 on: 6 bytes of tech requirements and 13 counters. */
static void clear_design_part_tally(void) { memset(LIN(MK_FP(MEM_SEL_DGROUP, DS_DESIGN_TALLY_maybe)), 0, DESIGN_TALLY_BYTES_maybe); }
/* 30:0x6112(player, 0, 0, 0xffff, &answer): a signed answer for the planet's owner, and a second value in the word it is given. */
static int16_t owner_answer_maybe(uint16_t player) { return (int16_t)CALL_ORIG(30, 0x6112, FAR_CDECL, 5, player, 0, 0, 0xffff, BP_NEAR(-0x2e)); }

/* The design of a slot as the scrap value counts it: the real one, or (for a race with trait 12) a copy that 7:0x2dbe adjusts. */
static const struct ShipDesign *design_for_scrap(uint32_t designs, uint16_t slot, bool copy)
{
  const struct ShipDesign *design = design_of(designs, slot);
  if (!copy) return design;
  DESIGN_COPY = *design;
  (void)CALL_ORIG(7, 0x2dbe, FAR_CDECL, 2, BP_NEAR(-0x150), MEM_SEL_DGROUP);
  return &DESIGN_COPY;
}

/* What the ships of a fleet are worth in one mineral, or in resources: the designs' costs times the ships, a quarter of that for the
 * designs flagged ShipDesign.flags bit 7. */
static uint32_t ships_value(const struct Fleet *f, bool resources, uint16_t mineral, bool copy)
{
  uint32_t value = 0;
  const uint32_t designs = player_designs(f->owner);
  for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
    const int16_t count = (int16_t)f->ship_count[slot];
    if (count <= 0) continue;
    const struct ShipDesign *design = design_for_scrap(designs, slot, copy);
    uint32_t amount = (uint32_t)count * (uint32_t)(resources ? design_resource_cost(design) : design_mineral_cost(design, mineral));
    if ((design->flags & DESIGN_FLAG_BIT7_maybe) != 0) amount = (uint32_t)idiv32((int32_t)amount, 4);
    value += amount;
  }
  return value;
}

/* How much of the ships' value comes back: 3/4 for a colony ship, 4/5 at a starbase (9/10 when the planet's owner has trait 5), a third
 * at a planet without one (9/20 with the trait) and in deep space. */
static uint32_t recovered_value(uint32_t value, uint16_t task, bool starbase, bool bonus, bool in_space)
{
  if (task == TASK_COLONIZE) return (uint32_t)idiv32((int32_t)(value * 3u), 4);
  if (in_space) return (uint32_t)idiv32((int32_t)value, 3);
  if (starbase) return (uint32_t)(bonus ? idiv32((int32_t)(value * 9u), 10) : idiv32((int32_t)(value * 4u), 5));
  return (uint32_t)(bonus ? idiv32((int32_t)(value * 9u), 20) : idiv32((int32_t)value, 3));
}

/* The fleet is used up: the colonists found the colony (colonize) or join the planet's if it is the owner's (scrap). */
static void finish_scrap(uint16_t fseg, uint16_t foff, uint16_t task)
{
  struct Fleet *f = FLEET(fseg, foff);
  g_current_player = NO_PLAYER;
  g_ui_flags_e_maybe = (uint8_t)(g_ui_flags_e_maybe & ~UI_FLAG_E_SCRAPPING_maybe);
  delete_order_messages(f->owner, f->id);
  f->flags = (uint8_t)(f->flags | FLEET_FLAG_EMPTY);
  if (task == TASK_COLONIZE) {
    (void)CALL_ORIG(23, 0x2fda, FAR_CDECL, 6, foff, fseg, BP_NEAR(-0xc2), MEM_SEL_DGROUP, (uint16_t)f->cargo[3], (uint16_t)(f->cargo[3] >> 16));
  } else if (f->planet_id != FLEET_IN_DEEP_SPACE) {
    struct Planet *p = PLANET(g_planet_table_segment_maybe, (uint16_t)(g_planet_table + (uint16_t)(f->planet_id * sizeof(struct Planet))));
    if (p->owner == f->owner) p->surface_amounts_maybe[CARGO_COLONISTS] += f->cargo[3];
  }
  finish_order(fseg, foff);
}

/* The messages about a scrap at a planet whose owner has trait 5: he also gets the resources of the ships. */
static void report_scrap_with_resources(uint16_t fseg, uint16_t foff, uint32_t total, uint32_t resources, bool starbase)
{
  const struct Fleet *f = FLEET(fseg, foff);
  const uint16_t plus = starbase ? 1 : 0;
  send_message(f->owner, (uint16_t)(MSG_SCRAP_RESOURCES_maybe + plus), PASS_OUT, fleet_name_maybe(fseg, foff), (uint16_t)total, (uint16_t)(total >> 16),
               PLANET_HERE.id, (uint16_t)resources, (uint16_t)(resources >> 16), 0);
  uint16_t message = (uint16_t)(MSG_SCRAP_OWNER_RESOURCES_maybe + plus);
  uint16_t detail = SCRAP_LOOP_END_maybe;
  if (starbase) {
    const int16_t answer = owner_answer_maybe(PLANET_HERE.owner);
    if (answer != 0) {
      message = answer < 0 ? MSG_SCRAP_OWNER_RESOURCES_LESS_maybe : MSG_SCRAP_OWNER_RESOURCES_MORE_maybe;
      detail = answer < 0 ? (uint16_t)(0xffff - (uint16_t)answer) : (uint16_t)(answer - 1);
    } else {
      detail = 0;
      PASS_OUT = PLANET_HERE.id;
    }
  } else {
    PASS_OUT = PLANET_HERE.id;
  }
  send_message(PLANET_HERE.owner, message, PASS_OUT, f->id, (uint16_t)total, (uint16_t)(total >> 16), PLANET_HERE.id, (uint16_t)resources,
               (uint16_t)(resources >> 16), detail);
}

/* The messages about a scrap into minerals: in deep space the salvage is created (30:0x183a), at a planet its owner is told. */
static void report_scrap(uint16_t fseg, uint16_t foff, uint32_t total, bool starbase)
{
  const struct Fleet *f = FLEET(fseg, foff);
  if (PLANET_HERE.id == NO_PLAYER) {
    SALVAGE_OUT = 0;
    (void)CALL_ORIG(30, 0x183a, FAR_CDECL, 5, BP_NEAR(-0x3c), BP_NEAR(-0xa6), MEM_SEL_DGROUP, fleet_id_owner(f->id), BP_NEAR(-0x6a));
    const uint16_t salvage_id = FAR_REC(struct MapObject, FP_SEG(SALVAGE_OUT), FP_OFF(SALVAGE_OUT))->id_kind;
    send_message2(f->owner, MSG_SCRAP_IN_SPACE_maybe, MSG_SUBJECT_SALVAGE_maybe, salvage_id, fleet_name_maybe(fseg, foff));
    return;
  }
  const uint16_t plus = starbase ? 1 : 0;
  send_message(f->owner, (uint16_t)(MSG_SCRAPPED_maybe + plus), PASS_OUT, fleet_name_maybe(fseg, foff), (uint16_t)total, (uint16_t)(total >> 16),
               PLANET_HERE.id, 0, 0, 0);
  uint16_t message = (uint16_t)(MSG_SCRAP_OWNER_maybe + plus);
  uint16_t detail = SCRAP_LOOP_END_maybe;
  if (starbase) {
    const int16_t answer = owner_answer_maybe(PLANET_HERE.owner);
    if (answer != 0) {
      message = answer < 0 ? MSG_SCRAP_OWNER_LESS_maybe : MSG_SCRAP_OWNER_MORE_maybe;
      detail = answer < 0 ? (uint16_t)(0xffff - (uint16_t)answer) : (uint16_t)(answer - 1);
    } else {
      detail = 0;
      PASS_OUT = PLANET_HERE.id;
    }
  }
  send_message(PLANET_HERE.owner, message, PASS_OUT, f->id, (uint16_t)total, (uint16_t)(total >> 16), PLANET_HERE.id, detail, 0, 0);
}

/* Colonize (any phase) and scrap (phase 1): a colony ship founds a colony, a scrapped fleet gives its ships' value to the planet (or
 * as salvage in space) with its minerals and colonists; the fleet is gone afterwards. */
static void colonize_or_scrap_order(uint16_t fseg, uint16_t foff, uint16_t task)
{
  struct Fleet *f = FLEET(fseg, foff);
  const uint16_t subject = (uint16_t)(f->id | FLEET_SUBJECT_FLAG);
  if (f->planet_id == FLEET_IN_DEEP_SPACE) {
    if (task == TASK_COLONIZE) {
      order_message2(f, MSG_COLONIZE_NO_PLANET_maybe, 0);
      finish_order(fseg, foff);
      return;
    }
    PLANET_HERE.id = NO_PLAYER;
    PLANET_HERE.owner = OWNER_NONE;
    PLANET_HERE.flags = (uint8_t)(PLANET_HERE.flags & ~PLANET_FLAG_HAS_STARBASE);
  } else if (!read_planet_here(f->planet_id) && task == TASK_COLONIZE) {
    finish_order(fseg, foff);
    return;
  }
  if (task == TASK_COLONIZE) {
    if (PLANET_HERE.owner != OWNER_NONE) {
      send_message(f->owner, MSG_COLONIZE_OWNED_maybe, subject, f->id, PLANET_HERE.id, PLANET_HERE.id, 0, 0, 0, 0);
      finish_order(fseg, foff);
      return;
    }
    if (f->cargo[3] == 0) {
      order_message2(f, MSG_COLONIZE_NO_COLONISTS_maybe, PLANET_HERE.id);
      finish_order(fseg, foff);
      return;
    }
  }
  /* does any ship carry a colonisation part (category 0x1000, item 0 or 1)? */
  clear_design_part_tally();
  bool colony_part = false;
  const uint32_t designs = player_designs(f->owner);
  for (uint16_t slot = 0; slot < SHIP_DESIGNS_PER_PLAYER; slot++) {
    if ((int16_t)f->ship_count[slot] <= 0) continue;
    const uint16_t tally_args[3] = {(uint16_t)(FP_OFF(designs) + (uint16_t)(slot * sizeof(struct ShipDesign))), FP_SEG(designs), f->owner};
    (void)far_call(16, 0x244c, tally_args, 3);
    const struct ShipDesign *design = design_of(designs, slot);
    for (uint16_t part = 0; part < design->slot_count; part++)
      if (design->slots[part].category == PART_CATEGORY_COLONY_maybe && (design->slots[part].item == 0 || design->slots[part].item == 1)) colony_part = true;
  }
  if (task == TASK_COLONIZE && !colony_part) {
    send_message(f->owner, MSG_COLONIZE_NO_MODULE_maybe, subject, f->id, PLANET_HERE.id, f->id, 0, 0, 0, 0);
    finish_order(fseg, foff);
    return;
  }
  /* the value of the ships and the cargo in each mineral goes to the planet (or into the scrap array) */
  const bool starbase = (PLANET_HERE.flags & PLANET_FLAG_HAS_STARBASE) != 0;
  const bool bonus = task == TASK_SCRAP && PLANET_HERE.owner != OWNER_NONE && player_trait(PLANET_HERE.owner, PLAYER_TRAIT_SCRAP_BONUS_maybe);
  const bool copy = player_trait(f->owner, PLAYER_TRAIT_COPY_DESIGNS_maybe);
  g_ui_flags_e_maybe = (uint8_t)(g_ui_flags_e_maybe | UI_FLAG_E_SCRAPPING_maybe);
  g_current_player = f->owner;
  uint32_t total = 0;
  for (uint16_t mineral = 0; mineral < SCRAP_FIELDS; mineral++) {
    const uint32_t value = recovered_value(ships_value(f, false, mineral, copy), task, starbase, bonus, PLANET_HERE.id == NO_PLAYER) + f->cargo[mineral];
    total += value;
    if (PLANET_HERE.id == NO_PLAYER)
      PLANET_HERE.surface_amounts_maybe[mineral] = value;
    else
      PLANET(g_planet_table_segment_maybe, (uint16_t)(g_planet_table + (uint16_t)(PLANET_HERE.id * sizeof(struct Planet))))->surface_amounts_maybe[mineral] +=
          value;
  }
  PASS_OUT = PLANET_HERE.id;
  if (bonus) {
    /* the owner of the planet also gets the resources of the ships, shared out by 9:0x4de8 */
    uint32_t resources = ships_value(f, true, 0, copy);
    if ((int32_t)resources > 0xffff) resources = 0xffff;
    const uint32_t word_table = drd32(DS_PLANET_WORD_TABLE_maybe);
    uint16_t *planet_word = (uint16_t *)LIN(MK_FP(FP_SEG(word_table), (uint16_t)(FP_OFF(word_table) + 2 * PLANET_HERE.id)));
    *planet_word = (uint16_t)(*planet_word + (uint16_t)resources);
    if ((int32_t)resources > 0 && PLANET_HERE.owner != OWNER_NONE) {
      const int32_t share = (int16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, BP_NEAR(-0xc2), MEM_SEL_DGROUP, PLANET_HERE.owner);
      resources = (uint32_t)idiv32((int32_t)(resources * (uint32_t)share), (int32_t)(resources + (uint32_t)share));
    }
    report_scrap_with_resources(fseg, foff, total, resources, starbase);
  } else {
    report_scrap(fseg, foff, total, starbase);
  }
  finish_scrap(fseg, foff, task);
}

/* The order of the first waypoint of one fleet, by its task. */
static void run_waypoint_order(uint16_t phase, uint16_t fseg, uint16_t foff, uint16_t *fleet_index)
{
  WP_COPY = *fleet_waypoint(fseg, foff, 0);
  const uint16_t task = waypoint_task(&WP_COPY);
  switch (task) {
    case TASK_TRANSPORT:
      carry_out_transport(phase, fseg, foff);
      break;
    case TASK_SCRAP:
      if (phase != PHASE_FIRST) break;
      colonize_or_scrap_order(fseg, foff, task);
      break;
    case TASK_COLONIZE:
      colonize_or_scrap_order(fseg, foff, task);
      break;
    case TASK_REMOTE_MINING:
      remote_mining_order(phase, fseg, foff);
      break;
    case TASK_ROUTE:
      if (route_order(phase, fseg, foff)) merge_order(phase, fseg, foff);
      break;
    case TASK_MERGE:
      merge_order(phase, fseg, foff);
      break;
    case TASK_TRANSFER:
      if (phase == PHASE_LAST) transfer_order(fseg, foff, fleet_index);
      break;
    case TASK_NONE:
    case TASK_LAY_MINES:
      lay_mines_order(phase, fseg, foff, task);
      break;
    default:
      break;
  }
}

/* execute_waypoint_orders: carry out the order of the first waypoint of every fleet, one phase of the turn.
 * 22:0x3ec6 FUN_10a8_3ec6, FAR CDECL (phase [BP+6]: 1 to 4), 8496 bytes (ENTER 0x16e,0; PUSH DI; PUSH SI; the jump tables at
 * 0x4312 and 0x4422 are inside it). 22:0x0e92 runs phases 1 and 2 before the movement of the turn and 3 and 4 after the map objects
 * have moved: odd phases unload, even phases load.
 * Walks the fleet pointer array (DS:0xBA, g_fleet_count entries, up to the first null entry). A fleet that is not empty
 * (flags bit 2) and not yet done (Fleet+0x76 bit 7) takes its first waypoint (the Waypoint, 18 bytes: x, y, target id, flags = task |
 * warp << 4 | target kind << 8, then five order words, one per cargo, order type << 12 | amount) and carries out its task:
 *   1 Transport           the five cargo orders against the target (a planet, a fleet, a point or a salvage object)
 *   2 Colonize, 5 Scrap   (scrap in phase 1) the colony is founded or the ships are turned into minerals for the planet or salvage
 *   3 Remote mining       (phase 3) 16:0x193e gives the mining rate, 5:0x3a72 mines the planet
 *   4 Merge, 8 Route      (even phases) join another fleet; a fleet at a planet with a route goes on to the route target
 *   6 Lay mines           (phase 3) mines into a minefield of the owner's or a new one; race 5 does it on the way
 *   9 Transfer            (phase 4) the fleet is given to another player
 * Phase 1 first clears bits 5 and 6 of Fleet+0x76. */
DECOMP(22, 0x3ec6, execute_waypoint_orders, FAR_CDECL)
void execute_waypoint_orders(uint16_t phase)
{
  FRAME_ENTER_REGS(0x16e, 2);
  uint16_t index = 0;
  while ((int16_t)index < (int16_t)g_fleet_count) {
    const uint32_t fleet = fleet_ptr_at((uint16_t)(index << 2));
    if (fleet == 0) break;
    const uint16_t fseg = FP_SEG(fleet), foff = FP_OFF(fleet);
    if (phase == PHASE_FIRST) set_fleet_move_state(fseg, foff, (uint8_t)(fleet_move_state(fseg, foff) & ~FLEET_STATE_PHASE_BITS));
    if ((FLEET(fseg, foff)->flags & FLEET_FLAG_EMPTY) == 0 && (fleet_move_state(fseg, foff) & FLEET_STATE_ORDERS_DONE_maybe) == 0)
      run_waypoint_order(phase, fseg, foff, &index);
    index++;
  }
  FRAME_LEAVE();
}
