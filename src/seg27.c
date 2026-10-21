/* Decompiled code segment 27 (NE segment 27, selector 0x00D8).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg27.c; Ghidra's pseudo-C in re/decomp/seg27 is only a reading aid.
 *
 * Segment 27 is the research and tech-browser user interface:
 *   27:0x0000  RESEARCHDLG: the research dialog procedure (research percent, current and next field)
 *   27:0x06ba  paint_research_dialog: draws the information panel of the research dialog
 *   27:0x134c  research_dialog_click: mouse click on the panel, bumps the research percent
 *   27:0x1546  research_level_cost: the research cost of one tech level
 *   27:0x1604  BROWSERDLG: the tech browser dialog procedure (steps through the tech items)
 *   27:0x1c68  BROWSERWNDPROC: the window procedure of the browser's item panel
 *   27:0x1e06  paint_tech_item_panel: draws one tech item (the long part of the function, 27:0x29ff
 *              to 0x492d, is the per-kind drawing code that the inventory does not list)
 *   27:0x492e  research_budget_at_percent: the resources a given research percent would spend
 *   27:0x49ee  research_cost_to_reach: the research cost of raising the tech levels to a target
 *   27:0x4af6  special_item_missing_maybe: has the player not yet got a special item
 * Data in the code segment: 27:0x062c is a table of 17 words (the tech item kinds, index 0 unused,
 * read by BROWSERDLG, 16 bit mask words in the order the browser steps through them) and 27:0x064e
 * a table of 27 dwords (the research cost of each tech level, read by research_level_cost).
 *
 * Names: RESEARCHDLG, BROWSERDLG and BROWSERWNDPROC are the export names. The other names describe
 * what a function does (CONVENTIONS rule f); names ending in _maybe are inferred and carry the
 * evidence in the comment. The old Ghidra names FUN_10d0_<off> are kept in the comment above each
 * function. The tech fields are the six Stars! fields (Energy, Weapons, Propulsion, Construction,
 * Electronics, Biotechnology: the order is inferred from the six radio buttons 0x431 to 0x436 and the
 * strings 0x5b to 0x60, not read from the strings).
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with n
 * the ENTER size and one push for each PUSH SI / PUSH DI. A callee that mirrors a frame is reached
 * through rt_call_far, callees outside this file are not decompiled yet: CALL_ORIG. Where the original
 * has already pushed some arguments of an API call when it calls a function of the program, those
 * words stay below SP during the call (the callee's frame, and the stack addresses it hands to the
 * APIs, sit below them): PENDING(n, call) lowers the emulated SP by n words for the call. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/stars.h"

/* ---- shared definitions ---------------------------------------------------------------------- */

enum {
  TECH_FIELD_COUNT = 6,   /* the six tech fields: 27:0x1546 sums six level bytes, the dialog has six radio buttons */
  TECH_LEVEL_MAX = 26,    /* the highest level: 27:0x49ee refuses a target above 0x1a; the cost table has 27 entries */
  /* DGROUP game flags bit 1 (DS:0x80): research costs twice as much. Evidence: 27:0x1546 and 27:0x49ee
   * double the level cost / the spent research when it is set (the "slow tech advance" game option). */
  GAME_FLAG_SLOW_TECH_MAYBE = 0x02,
  CS_TECH_COST_TABLE = 0x064e, /* 27 dwords in this code segment: the cost of tech level 0 to 26 */
  SHIPDESIGN_COPY_BYTES = 0x7b /* the browser copies a hull record up to (not including) struct ShipDesign.version */
};

/* The Player record (stars.h struct Player, 0xC0 bytes at DS:0x5A64) as the research code reads it.
 * The bytes between +0x10 and +0x37 and +0x38 to +0x3d are "unknown" padding in stars.h; this segment
 * shows what they hold. */
#pragma pack(push, 1)
struct PlayerResearch {
  uint8_t before_tech_levels[0x1a];
  int8_t tech_level[TECH_FIELD_COUNT];                  /* +0x1a: the six tech levels (27:0x1546, 27:0x49ee) */
  uint32_t research_spent_maybe[TECH_FIELD_COUNT];      /* +0x20: research spent per field; 27:0x49ee subtracts it from the cost to reach a level */
  int8_t research_percent;                              /* +0x38: Player.cost_adjust_pct, the share of resources spent on research (27:0x0000 edits it) */
  uint8_t research_field_bits;                          /* +0x39: low nibble = field being researched, high nibble = field researched next (27:0x0000, 27:0x06ba) */
  uint32_t research_resources_maybe;                    /* +0x3a: printed by 27:0x06ba next to the label of string 0x4f */
  uint8_t before_special_items[0x52 - 0x3e];
  uint16_t special_items_owned_maybe;                   /* +0x52: one bit per special item, tested by 27:0x4af6 */
  uint8_t after_special_items[0xc0 - 0x54];
};
STATIC_SIZE(struct PlayerResearch, 0xC0);
STATIC_OFFSET(struct PlayerResearch, tech_level, 0x1a);
STATIC_OFFSET(struct PlayerResearch, research_spent_maybe, 0x20);
STATIC_OFFSET(struct PlayerResearch, research_percent, 0x38);
STATIC_OFFSET(struct PlayerResearch, research_field_bits, 0x39);
STATIC_OFFSET(struct PlayerResearch, research_resources_maybe, 0x3a);
STATIC_OFFSET(struct PlayerResearch, special_items_owned_maybe, 0x52);
#pragma pack(pop)

/* The offset of the player's record in DGROUP: a 16-bit product, as in the original. */
static inline uint16_t player_row(uint16_t player) {
  return (uint16_t)(DS_PLAYERS + (uint16_t)(player * sizeof(struct Player)));
}
/* The fields below are reached with 16-bit wrapping offsets like the original's [bx + disp]. */
static inline int8_t *player_tech_level(uint16_t player, uint16_t field) {
  return (int8_t *)LINN((uint16_t)(player_row(player) + offsetof(struct PlayerResearch, tech_level) + field));
}
static inline int8_t *player_research_percent(uint16_t player) {
  return (int8_t *)LINN((uint16_t)(player_row(player) + offsetof(struct PlayerResearch, research_percent)));
}
static inline uint8_t *player_research_field_bits(uint16_t player) {
  return (uint8_t *)LINN((uint16_t)(player_row(player) + offsetof(struct PlayerResearch, research_field_bits)));
}
static inline uint16_t *player_special_items(uint16_t player) {
  return (uint16_t *)LINN((uint16_t)(player_row(player) + offsetof(struct PlayerResearch, special_items_owned_maybe)));
}
/* The dword research_spent_maybe[field]: the original indexes it as ((player * 0x30 + field) << 2) + 0x5a84. */
static inline uint32_ua *player_research_spent(uint16_t player, uint16_t field) {
  return (uint32_ua *)LINN((uint16_t)((uint16_t)((uint16_t)(player * 0x30 + field) << 2) + DS_PLAYERS +
                                      offsetof(struct PlayerResearch, research_spent_maybe)));
}
/* A word or dword of this code segment (the original reads them with CS: overrides). */
static inline uint16_t cs_word(uint16_t off) { return *(uint16_ua *)LIN(MK_FP(SEG_SEL(27), off)); }
static inline uint32_t cs_dword(uint16_t off) {
  return (uint32_t)cs_word(off) | ((uint32_t)cs_word((uint16_t)(off + 2)) << 16);
}

/* ---- the DGROUP data of the research and tech-browser screens ---------------------------------------
 * globals.h has no name for these words (they are g_DAT_...): the names below are provisional, the
 * evidence is the code of this segment. */
#define g_research_percent_edit_maybe g_DAT_1110_0d0a    /* the research percent being edited: RESEARCHDLG loads it from the player's byte, research_dialog_click moves it by 1 or 10, OK stores it back */
#define g_research_info_top_maybe g_DAT_1110_0d10        /* y of the first "resources" line the panel draws (-1: not drawn); clicks and the cursor test the band below it */
#define g_tech_panel_locked_maybe g_DAT_1110_0c7e        /* BROWSERWNDPROC hands the mouse to DefWindowProc while it is non-zero */
#define g_research_radio_width_maybe g_DAT_1110_2660     /* the widest of the six field names + 0x40: the width of the radio buttons */
#define g_research_label_width_maybe g_DAT_1110_2672     /* the widest of the six labels of strings 0x4c to 0x51, then + the width of string 0x37e + 0x30 */
#define g_research_label_width_copy_maybe g_DAT_1110_53de /* the first value of g_research_label_width_maybe: the x offset of the second text column */
#define g_research_column_x_maybe g_DAT_1110_5a5e        /* x of the right column of the panel: width of string 0x1b8 + the radio width + 0x28 */
#define g_research_item_count_maybe g_DAT_1110_2888      /* how many tech items the panel lists (at most 8) */
#define g_research_list_top_maybe g_DAT_1110_4b6e        /* y of the first listed tech item */
#define g_research_budget_maybe g_DAT_1110_26b2          /* the dword research_budget_at_percent returned for the edited percent */
#define g_research_resources_total_maybe g_DAT_1110_27b6 /* the dword sum of the resources of the player's planets (9:0x4de8) */
#define g_tech_arg_record_maybe g_DAT_1110_252e         /* the dword (pointer to the record) 2:0x50f8 stores in the tech item request */
#define g_research_field_maybe (*(uint16_t *)&g_zip_selected_preset) /* word access to the byte globals.h names; the field selected in the dialog (0 to 5): the low nibble of the player's research byte */
/* DS:0x0bc0, the descriptor 24:0x091e shows (stars.h g_info_descriptor is a dword there, the code here writes words) */
#define g_info_kind_maybe (*(uint16_t *)&g_info_descriptor)              /* what to show: 9 a tech item request, 0xa a text, 0xe a hull record */
#define g_info_arg_a_maybe (*(uint16_t *)((uint8_t *)&g_info_descriptor + 2)) /* first argument: a pointer, a request dword's low word or a width */
#define g_info_request_maybe (*(uint32_ua *)((uint8_t *)&g_info_descriptor + 2)) /* the same two words as the tech item request (kind, index) of a listed item */
#define g_info_arg_c_maybe (*(uint16_t *)&g_info_descriptor_plus06)  /* third argument word (always 0 here) */
/* A far pointer to a local of the running frame (the original passes SS:BP+off to the API). */
#define BP_FP(off) MK_FP(rt_ss, (uint16_t)(rt_bp + (off)))
/* PENDING(n, call): the original has pushed n words of an API call's arguments when it makes the nested call
 * that produces the next one; those words stay below SP during the nested call. */
#define PENDING(nwords, call)                                  \
  ({                                                          \
    uint16_t sp_pending_ = rt_sp;                             \
    rt_sp = (uint16_t)(rt_sp - 2 * (nwords));                 \
    __typeof__(call) value_pending_ = (call);                 \
    rt_sp = sp_pending_;                                      \
    value_pending_;                                           \
  })
enum {
  DS_RESEARCH_DLG_POS = 0x0d0c,       /* 6 bytes (-1 at start): the remembered dialog position, 8:0x97a2 reads and writes it */
  DS_BROWSER_DLG_POS = 0x0d3a,        /* the same for the tech browser dialog */
  DS_STR_LEVEL = 0x0d12,              /* "Level" */
  DS_STR_999999 = 0x0d18,             /* "999999": the sample whose width sizes a number column */
  DS_FMT_TECH_LEVEL = 0x0d1f,         /* "%s, Tech Level %d" */
  DS_STR_100 = 0x0d31,                /* "100" */
  DS_STR_PERCENT_SIGN = 0x0d35,       /* "%" */
  DS_STR_PERCENT_SIGN_2 = 0x0d37,     /* "%" (a second copy, the one that is drawn) */
  DS_STR_99 = 0x0d40,                 /* "99" */
  DS_STR_KT = 0x0d43,                 /* "kT" */
  DS_STR_BLEEDING_EDGE = 0x0d46,      /* "Bleeding Edge" */
  DS_STR_0 = 0x0d54,                  /* "0" */
  DS_STR_MG_FORMAT = 0x0d56,          /* "%dmg" */
  DS_STR_NUM_SAMPLE = 0x0d5b,         /* "Num": sample text for the width of the defence chart's number column */
  DS_STR_STANDARD = 0x0d5f,           /* "Standard" */
  DS_STR_SMART = 0x0d68,              /* "Smart" */
  DS_STR_NUM = 0x0d6e,                /* "Num" */
  DS_STR_0_AXIS = 0x0d72,             /* "0" */
  DS_TEXT_BUFFER = 0x5866,            /* g_text_buffer: the scratch string the drawing code formats into */
  DS_TECH_REQUEST = 0x252a,           /* the tech item request: kind word, index byte (+2), the record pointer (+4) 2:0x50f8 fills */
  DS_INFO_ARG_A = 0x0bc2,             /* g_info_arg_a_maybe as an address: 2:0x50f8 is asked about the request stored there */
  DS_BROWSER_DESIGN_COPY = 0x26b6,    /* a 0x7b-byte copy of the hull record the browser shows (struct ShipDesign) */
  DS_RESEARCH_UP_RECT = 0x29d6,       /* the two small arrow buttons beside the research percent: RECT of the one that raises it ... */
  DS_RESEARCH_DOWN_RECT = 0x29de,     /* ... and of the one that lowers it */
  DS_RESEARCH_ITEM_REQUESTS = 0x4c52, /* up to 8 dwords: the tech item request of each listed item (kind word, index word) */
  DS_CLASS_STARSBROWSER = 0x01dc,     /* "starsbrowser": the window class of BROWSERWNDPROC */
  CS_TECH_KIND_TABLE = 0x062c,        /* 17 words in this code segment: [0] unused, [1..16] the tech item kinds in the order the browser steps through them */
  CS_TECH_KIND_TABLE_END = 0x064e,
  CMD_BROWSER_ONLY_AVAILABLE = 0x10a, /* the check box of the browser (checked: only the items the player can use? inferred) */
  CMD_BROWSER_KIND_COMBO = 0x10b,     /* the combo box of the tech item kinds */
  CMD_BROWSER_PREVIOUS = 0x42e,       /* the buttons that step to the previous / next item */
  CMD_BROWSER_NEXT = 0x42f,
  CMD_RESEARCH_FIELD_FIRST = 0x431,   /* the six radio buttons of the research fields */
  CMD_RESEARCH_FIELD_LAST = 0x436,
  CMD_RESEARCH_NEXT_COMBO = 0x43b,    /* the combo box of the field to research next */
  CMD_RESEARCH_HELP = 0x76,
  CMD_RESEARCH_CLOSE = 2
};

/* Three halves of a 16-bit value, rounded toward zero: ax = 3 * ax; cdq; sub ax, dx; sar ax, 1. */
/* IDIV with the divisor in a register: the original traps (#DE) on a zero divisor and on a quotient that does not fit;
 * the C version stops with a message (CONVENTIONS rule b). `where` is the offset of the IDIV in segment 27. */
static int32_t idiv32(int32_t num, int32_t den, uint16_t where)
{
  if (den == 0 || (num == INT32_MIN && den == -1)) {
    fprintf(stderr, "27:0x%04x: divide error (IDIV %d by %d)\n", where, num, den);
    abort();
  }
  return num / den;
}
static int16_t idiv16(int16_t num, int16_t den, uint16_t where)
{
  if (den == 0 || (num == INT16_MIN && den == -1)) {
    fprintf(stderr, "27:0x%04x: divide error (IDIV %d by %d)\n", where, num, den);
    abort();
  }
  return (int16_t)(num / den);
}
static inline int16_t three_halves(int16_t v) { return (int16_t)((int16_t)(uint16_t)(v + v + v) / 2); }

/* The `what` word of paint_research_dialog (27:0x06ba): the low byte selects the boxes, the high byte the lines. */
enum {
  PAINT_LEVELS_BOX = 0x0003,        /* low byte: the box with the six tech levels */
  PAINT_INVALIDATE_ONLY = 0x0004,   /* low byte: with no box bit, only ask for a repaint of the region */
  PAINT_BOX_ANY = 0x000f,           /* low byte: any box bit: the labels are already on the screen */
  PAINT_COST_LINES = 0x0300,        /* high byte: the cost of the next level of the field (and the turns it takes) */
  PAINT_COST_NUMBERS = 0x0200,      /* high byte bit: with no box, only the numbers of those lines */
  PAINT_RESOURCES_BOX = 0x0400,     /* high byte: the "resources" box title */
  PAINT_RESOURCES_TOTAL = 0x0800,   /* high byte: the resources of all planets */
  PAINT_RESOURCES_SPENT = 0x1000,   /* high byte: the player's research resources */
  PAINT_PERCENT_LABEL = 0x2000,     /* high byte: the label of the research percent */
  PAINT_PERCENT_VALUE = 0x4000      /* high byte: the percent (with the arrows when drawn with the labels) */
};
/* PUSH on the emulated stack: the variable arguments of _wsprintf and arguments still pending. */
static inline void push_word(uint16_t v) {
  rt_sp = (uint16_t)(rt_sp - 2);
  memcpy(LIN(MK_FP(rt_ss, rt_sp)), &v, 2);
}
static inline void push_dword(uint32_t v) {
  push_word((uint16_t)(v >> 16));
  push_word((uint16_t)v);
}
static inline void pop_words(uint16_t n) { rt_sp = (uint16_t)(rt_sp + 2 * n); }
static inline uint32_t ds_far(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }
/* The length of a NUL-terminated string (the original scans for the NUL with REPNE SCASB). */
static inline uint16_t cstr_length(const uint8_t *s) {
  uint16_t n = 0;
  while (s[n] != 0) n++;
  return n;
}
/* The width of the text at DS:off, count characters. */
static inline int16_t text_width(uint16_t hdc, uint16_t off, int16_t count) {
  return (int16_t)(uint16_t)GetTextExtent(hdc, ds_far(off), count);
}
static inline void text_out_ds(uint16_t hdc, int16_t x, int16_t y, uint16_t off, int16_t count) {
  (void)TextOut(hdc, x, y, ds_far(off), count);
}

/* ---- 27:0x4af6 ---------------------------------------------------------------------------------- */

/* special_item_missing_maybe: 27:0x4af6 (Ghidra FUN_10d0_4af6). 240 bytes.
 * PUSH BP; MOV BP,SP; PUSH DI; PUSH SI. FAR CDECL, one argument: a near pointer to a tech item request
 * (word kind at +0, byte index at +2; the record 2:0x50f8 and 2:0x50d8 take). AX only.
 * Twelve special items (kind, index) each own one bit of the word at +0x52 of the current player's
 * record (DS:0x5A64 + 0xC0 * DS:0x14C); the request is such an item and that bit is clear -> 1,
 * otherwise (no current player, another item, bit set) 0. 2:0x50f8 asks it for the kinds that are
 * hidden until a player has the item. Name provisional: the twelve items look like the ones that come
 * from outside the tech tree (the bits are set by 9:0x6372 messages, not shown here). */
DECOMP(27, 0x4af6, special_item_missing_maybe, FAR_CDECL)
uint16_t special_item_missing_maybe(uint16_t request)
{
  FRAME_ENTER_REGS(0, 2);
  static const struct { uint16_t kind; uint8_t index; uint16_t bit; } items[] = {
    {0x1000, 4, 0x0001}, {0x0800, 4, 0x0002}, {0x0004, 6, 0x0004}, {0x0008, 9, 0x0008},
    {0x0080, 6, 0x0010}, {0x0040, 8, 0x0020}, {0x0020, 7, 0x0040}, {0x0010, 0x12, 0x0080},
    {0x4000, 0x1e, 0x0100}, {0x0001, 8, 0x0200}, {0x8000, 0x0e, 0x0400}, {0x1000, 9, 0x0800},
  };
  uint16_t result = 0;
  if (g_current_player != 0xffff) {
    uint16_t bit = 0;
    uint16_t index = NEAR_AT(uint8_t, (uint16_t)(request + 2));
    uint16_t kind = NEAR_AT(uint16_ua, request);
    for (uint16_t k = 0; k < sizeof items / sizeof items[0]; k++) {
      if (items[k].kind == kind && items[k].index == index) bit = items[k].bit;
    }
    if (bit != 0 && (*player_special_items(g_current_player) & bit) == 0) result = 1;
  }
  FRAME_LEAVE();
  return result;
}

/* ---- 27:0x1546 ---------------------------------------------------------------------------------- */

/* research_level_cost: 27:0x1546 (Ghidra FUN_10d0_1546). 190 bytes. ENTER 8,0; PUSH DI; PUSH SI.
 * FAR CDECL (field [BP+6], level [BP+8], player [BP+0xA]); DX:AX.
 * The research cost of reaching `level` in tech field `field` for `player`: ten times the sum of the
 * player's six tech levels (signed bytes at +0x1a of the record, 16-bit product) plus the table entry
 * of the level (27:0x064e + 4 * level, a dword in this code segment). The player's field cost modifier
 * (28:0x21ee(record, field + 8), the signed byte at +0x46 + field) then scales it: 1 leaves it, below 1
 * the cost grows to twice minus a quarter (x * 7 / 4), above 1 it halves (a signed division by 2). With
 * the slow-tech game flag (DS:0x80 bit 1) the result is doubled (32-bit). BP-8 holds the record offset,
 * BP-4 the running cost. */
DECOMP(27, 0x1546, research_level_cost, FAR_CDECL)
uint32_t research_level_cost(int16_t field, int16_t level, int16_t player)
{
  FRAME_ENTER_REGS(8, 2);
  BP_LOCAL(uint16_t, -8) = (uint16_t)((uint16_t)player * sizeof(struct Player));
  int16_t level_sum = 0;
  for (int16_t k = 0; k < TECH_FIELD_COUNT; k++) level_sum = (int16_t)(level_sum + *player_tech_level((uint16_t)player, (uint16_t)k));
  int16_t scaled_sum = (int16_t)((uint16_t)level_sum * 10);
  uint16_t table_at = (uint16_t)((uint16_t)level << 2);
  uint32_t cost = (uint32_t)(int32_t)scaled_sum + cs_dword((uint16_t)(table_at + CS_TECH_COST_TABLE));
  BP_LOCAL(uint32_t, -4) = cost;
  uint16_t modifier = (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, player_row((uint16_t)player), (uint16_t)(field + 8));
  uint16_t above_one = (uint16_t)(modifier - 1);
  if (above_one != 0) {
    if ((int16_t)above_one < 0) cost = (cost << 1) - (uint32_t)((int32_t)cost >> 2);
    else cost = (uint32_t)((int32_t)cost / 2);
    BP_LOCAL(uint32_t, -4) = cost;
  }
  if ((g_game_flags & GAME_FLAG_SLOW_TECH_MAYBE) != 0) {
    cost = cost + cost;
    BP_LOCAL(uint32_t, -4) = cost;
  }
  FRAME_LEAVE();
  return cost;
}

/* ---- 27:0x492e ---------------------------------------------------------------------------------- */

/* research_budget_at_percent: 27:0x492e (Ghidra FUN_10d0_492e). 191 bytes. ENTER 0x16,0; PUSH DI;
 * PUSH SI. FAR CDECL (percent: the byte at [BP+6]); DX:AX.
 * What the current player would spend on research per turn at `percent` (the player's research percent
 * byte, +0x38 of the record, is set to it for the calculation and restored): the sum, over the
 * planets of the current player, of the planet's resources (9:0x4de8(planet, player), signed 16 bit)
 * or, for a planet with a production queue that holds entries, the resources 26:0x29da answers for
 * target -1 (its first_turn out word, BP-0x14: the planet's resources plus the research share, signed
 * 16 bit). BP-0x14 and BP-0x16 are the two out words of 26:0x29da. */
DECOMP(27, 0x492e, research_budget_at_percent, FAR_CDECL)
uint32_t research_budget_at_percent(uint8_t percent)
{
  FRAME_ENTER_REGS(0x16, 2);
  uint32_t total = 0;
  BP_LOCAL(uint32_t, -8) = total;
  int8_t saved_percent = *player_research_percent(g_current_player);
  BP_LOCAL(int8_t, -0xd) = saved_percent;
  *player_research_percent(g_current_player) = (int8_t)percent;
  uint16_t planet_seg = g_planet_table_segment_maybe;
  uint16_t at = g_planet_table;
  uint16_t end = (uint16_t)(at + (uint16_t)(g_planet_count * sizeof(struct Planet)));
  BP_LOCAL(uint16_t, -0x10) = planet_seg;
  if (end > at) {
    for (; at < end; at = (uint16_t)(at + sizeof(struct Planet))) {
      if (FAR_REC(struct Planet, planet_seg, at)->owner != g_current_player) continue;
      int32_t resources = (int16_t)(uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, at, planet_seg, g_current_player);
      FARPTR queue = FAR_REC(struct Planet, planet_seg, at)->queue;
      if (queue != 0 && FAR_REC(struct ProdQueueHdr, FP_SEG(queue), FP_OFF(queue))->count != 0) {
        const uint16_t args[7] = {at, planet_seg, 0, 0, 0xffff, (uint16_t)(rt_bp - 0x14), (uint16_t)(rt_bp - 0x16)};
        (void)rt_call_far(MK_FP(SEG_SEL(26), 0x29da), FAR_CDECL, args, 7);
        resources = (int16_t)BP_LOCAL(uint16_t, -0x14);
      }
      total += (uint32_t)resources;
      BP_LOCAL(uint32_t, -8) = total;
    }
  }
  *player_research_percent(g_current_player) = BP_LOCAL(int8_t, -0xd);
  FRAME_LEAVE();
  return total;
}

/* ---- 27:0x49ee ---------------------------------------------------------------------------------- */

/* research_cost_to_reach: 27:0x49ee (Ghidra FUN_10d0_49ee). 264 bytes. ENTER 0x18,0; PUSH DI; PUSH SI.
 * FAR CDECL (target_levels: a far pointer to six signed level bytes, [BP+6]); DX:AX.
 * The research the current player still has to spend to raise the six tech levels to the target
 * levels: -1 when any target is above 26. For each field below its target the cost of every level up
 * to the target is added with 27:0x1546 (the player's level in the record is raised one at a time, so
 * every step is costed with the levels reached so far), the research already spent in the field
 * (doubled with the slow-tech flag) is subtracted, and a field total below zero counts as zero. The six
 * levels are put back at the end (the copy is at BP-0x16). */
DECOMP(27, 0x49ee, research_cost_to_reach, FAR_CDECL)
uint32_t research_cost_to_reach(uint32_t target_levels)
{
  FRAME_ENTER_REGS(0x18, 2);
  const uint16_t tseg = FP_SEG(target_levels), toff = FP_OFF(target_levels);
  uint32_t total = 0;
  BP_LOCAL(uint32_t, -0xe) = total;
  uint16_t k = 0;
  while (k < TECH_FIELD_COUNT && *FAR_REC(int8_t, tseg, toff + k) <= TECH_LEVEL_MAX) k++;
  if (k < TECH_FIELD_COUNT) {
    FRAME_LEAVE();
    return 0xffffffffu;
  }
  for (uint16_t field = 0; field < TECH_FIELD_COUNT; field++) {
    int8_t current = *player_tech_level(g_current_player, field);
    BP_LOCAL(int8_t, (int16_t)(-0x16 + field)) = current;
    if (*FAR_REC(int8_t, tseg, toff + field) > current) {
      uint32_t spent = *player_research_spent(g_current_player, field);
      BP_LOCAL(uint32_t, -4) = spent;
      if ((g_game_flags & GAME_FLAG_SLOW_TECH_MAYBE) != 0) {
        spent = spent + spent;
        BP_LOCAL(uint32_t, -4) = spent;
      }
      uint32_t field_cost = 0u - spent;
      BP_LOCAL(uint32_t, -0xa) = field_cost;
      if (*FAR_REC(int8_t, tseg, toff + field) > *player_tech_level(g_current_player, field)) {
        do {
          BP_LOCAL(uint16_t, -6) = field;
          const uint16_t args[3] = {field, (uint16_t)(int16_t)(*player_tech_level(g_current_player, field) + 1), g_current_player};
          field_cost += rt_call_far(MK_FP(SEG_SEL(27), 0x1546), FAR_CDECL, args, 3);
          BP_LOCAL(uint32_t, -0xa) = field_cost;
          *player_tech_level(g_current_player, field) += 1;
        } while (*FAR_REC(int8_t, tseg, toff + field) > *player_tech_level(g_current_player, field));
      }
      if ((int16_t)(field_cost >> 16) < 0) field_cost = 0;
      total += field_cost;
      BP_LOCAL(uint32_t, -0xe) = total;
    }
  }
  for (uint16_t field = 0; field < TECH_FIELD_COUNT; field++)
    *player_tech_level(g_current_player, field) = BP_LOCAL(int8_t, (int16_t)(-0x16 + field));
  FRAME_LEAVE();
  return total;
}

/* ---- 27:0x1c68 ---------------------------------------------------------------------------------- */

/* BROWSERWNDPROC: 27:0x1c68 (Ghidra and export name BROWSERWNDPROC). 414 bytes. ENTER 0x2c,0; PUSH DI;
 * PUSH SI. FAR PASCAL (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]); DX:AX.
 * The window procedure of the item panel of the tech browser (BROWSERDLG creates it). The panel
 * shows the tech item request at DS:0x252a.
 *   WM_CREATE (1)        returns 0.
 *   WM_PAINT (0xf)       paints with 27:0x1e06(hdc, client width, client height, DS:0x252a); returns 0.
 *   WM_ERASEBKGND (0x14) fills the client area with the button face brush DS:0x10; returns 1.
 *   WM_SETCURSOR (0x20) and WM_LBUTTONDOWN (0x201): the mouse position (the cursor position in client
 *     coordinates, or lParam) is looked at only when the panel shows a starbase hull (kind 0x400) or a ship
 *     hull (kind 0x4000), DS:0xc7e is 0 and the point is in the 0x40 by 0x40 picture box (x 5 to 0x44,
 *     y from DS:0x54ee + 5 for 0x40 rows); there the cursor becomes the info cursor DS:0x4ac8, and a click
 *     copies the hull record (2:0x0000 for 0x400, 2:0x507c for 0x4000, indexed by the request's item byte
 *     DS:0x252c) to DS:0x26b6, clears the part count of every slot of the copy and shows it with
 *     24:0x091e(hwnd, x, y) (info kind 0xe, argument = the copy). Both return 1.
 *   Everything else (and the mouse outside the picture box) goes to DefWindowProc. */
DECOMP(27, 0x1c68, BROWSERWNDPROC, FAR_PASCAL)
uint32_t BROWSERWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x2c, 2);
  if (msg == WM_CREATE) {
    FRAME_LEAVE();
    return 0;
  }
  if (msg == WM_PAINT) {
    uint16_t hdc = BeginPaint(hwnd, BP_FP(-0x2c));
    GetClientRect(hwnd, BP_FP(-0xc));
    const uint16_t args[4] = {hdc, BP_LOCAL(uint16_t, -8), BP_LOCAL(uint16_t, -6), DS_TECH_REQUEST};
    (void)rt_call_far(MK_FP(SEG_SEL(27), 0x1e06), FAR_CDECL, args, 4);
    EndPaint(hwnd, BP_FP(-0x2c));
    FRAME_LEAVE();
    return 0;
  }
  if (msg == WM_ERASEBKGND) {
    GetClientRect(hwnd, BP_FP(-0xc));
    (void)FillRect(wParam, BP_FP(-0xc), g_brush_btnface);
    FRAME_LEAVE();
    return 1;
  }
  if (msg == WM_SETCURSOR) {
    GetCursorPos(BP_FP(-4));
    ScreenToClient(hwnd, BP_FP(-4));
  } else if (msg == WM_LBUTTONDOWN) {
    BP_LOCAL(uint16_t, -4) = (uint16_t)lParam;
    BP_LOCAL(uint16_t, -2) = (uint16_t)(lParam >> 16);
  } else {
    uint32_t def = DefWindowProc(hwnd, msg, wParam, lParam);
    FRAME_LEAVE();
    return def;
  }
  if ((g_tech_arg_mask_maybe != 0x4000 && g_tech_arg_mask_maybe != 0x400) || g_tech_panel_locked_maybe != 0 ||
      BP_LOCAL(int16_t, -4) < 5 || BP_LOCAL(int16_t, -4) >= 0x45 ||
      (int16_t)(g_cell_height_10pt + 5) > BP_LOCAL(int16_t, -2) || (int16_t)(g_cell_height_10pt + 0x45) <= BP_LOCAL(int16_t, -2)) {
    uint32_t def = DefWindowProc(hwnd, msg, wParam, lParam);
    FRAME_LEAVE();
    return def;
  }
  if (msg == WM_LBUTTONDOWN) {
    g_info_kind_maybe = 0xe;
    const uint16_t index[1] = {g_tech_arg_item_low_maybe};
    uint32_t hull = rt_call_far(MK_FP(SEG_SEL(2), g_tech_arg_mask_maybe == 0x400 ? 0x0000 : 0x507c), FAR_CDECL, index, 1);
    memcpy(LINN(DS_BROWSER_DESIGN_COPY), LIN(hull), SHIPDESIGN_COPY_BYTES);
    struct ShipDesign *copy = (struct ShipDesign *)LINN(DS_BROWSER_DESIGN_COPY);
    for (uint16_t k = 0; k < copy->slot_count; k++) (copy->slots + k)->count = 0;
    g_info_arg_a_maybe = DS_BROWSER_DESIGN_COPY;
    g_info_descriptor_plus04 = MEM_SEL_DGROUP;
    g_info_arg_c_maybe = 0;
    (void)rt_call_far(MK_FP(SEG_SEL(24), 0x091e), FAR_CDECL,
        (const uint16_t[]){hwnd, BP_LOCAL(uint16_t, -4), BP_LOCAL(uint16_t, -2)}, 3);
  } else {
    (void)SetCursor(g_cursor_info_panel);
  }
  FRAME_LEAVE();
  return 1;
}

/* ---- 27:0x134c ---------------------------------------------------------------------------------- */

/* research_dialog_click: 27:0x134c (Ghidra FUN_10d0_134c). 506 bytes. ENTER 0x2c,0; PUSH DI; PUSH SI.
 * FAR CDECL (hwnd [BP+6], x [BP+8], y [BP+0xA], modifiers [BP+0xC], the byte of wParam: bit 2 = shift).
 * AX = 1 when the click was used, 0 otherwise. RESEARCHDLG calls it for WM_LBUTTONDOWN and
 * WM_LBUTTONDBLCLK with the client position of the click.
 *  - On the up arrow (rect DS:0x29d6) or the down arrow (rect DS:0x29de) beside the research percent:
 *    while the mouse button is held (8:0x9aea starts the press, 8:0x9b74 answers "still held" and waits
 *    for the repeat) the edited percent DS:0xd0a moves by 1, or by 10 with shift, clamped to 0 to 100;
 *    when it changed, DS:0x26b2 = 27:0x492e(percent) and the panel is repainted with
 *    27:0x06ba(hwnd, dc, client rect, 0x4200). The press state (24 bytes, with the DC in its second word)
 *    is at BP-0x2a, the client rect at BP-0x12.
 *  - On a listed tech item (x 13 to the column x - 13, y inside the list that starts at DS:0x4b6e,
 *    line height DS:0x267a, DS:0x2888 lines): the item's request (dword at DS:0x4c52 + 4 * line) is copied
 *    to DS:0xbc2, 2:0x50f8 looks it up, and the info panel 24:0x091e is shown for kind 9.
 *  - On the "resources" text (x right of DS:0x5a5e, y in the band of 3 lines from DS:0xd10): the info
 *    panel shows the text of string 0x148 or 0x140 (kind 0xa, argument DS:0x2672); which one depends on
 *    the traits 4 and 0xc of the current player's race and on whether the click is in the upper half. */
DECOMP(27, 0x134c, research_dialog_click, FAR_CDECL)
uint16_t research_dialog_click(uint16_t hwnd, int16_t x, int16_t y, uint16_t modifiers)
{
  FRAME_ENTER_REGS(0x2c, 2);
  const uint32_t point = (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
  int16_t direction;
  uint16_t arrow_rect;
  uint16_t frame_id;
  if (PtInRect(MK_FP(MEM_SEL_DGROUP, DS_RESEARCH_UP_RECT), point) != 0) {
    direction = 1;
    arrow_rect = DS_RESEARCH_UP_RECT;
    frame_id = 0xa0;
  } else if (PtInRect(MK_FP(MEM_SEL_DGROUP, DS_RESEARCH_DOWN_RECT), point) != 0) {
    direction = -1;
    arrow_rect = DS_RESEARCH_DOWN_RECT;
    frame_id = 0xa1;
  } else {
    uint16_t info_text;
    if (!((int16_t)g_research_list_top_maybe > y ||
          (int16_t)((int16_t)g_research_item_count_maybe * (int16_t)g_text_line_height + (int16_t)g_research_list_top_maybe) <= y ||
          x <= 0xc || (int16_t)(g_research_column_x_maybe - 0xc) <= x)) {
      /* a listed tech item */
      int16_t line = idiv16((int16_t)(y - (int16_t)g_research_list_top_maybe), (int16_t)g_text_line_height, 0x1475);
      g_info_request_maybe = NEAR_AT(uint32_ua, (uint16_t)((uint16_t)(line << 2) + DS_RESEARCH_ITEM_REQUESTS));
      const uint16_t lookup[1] = {DS_INFO_ARG_A};
      (void)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, lookup, 1);
      g_info_kind_maybe = 9;
    } else {
      if ((int16_t)g_research_info_top_maybe == -1 || y < (int16_t)g_research_info_top_maybe ||
          (int16_t)((int16_t)g_text_line_height * 3 + (int16_t)g_research_info_top_maybe) <= y ||
          (int16_t)g_research_column_x_maybe >= x) {
        FRAME_LEAVE();
        return 0;
      }
      if ((uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 4) == 0) {
        info_text = 0x148;
      } else {
        int16_t half_band = three_halves((int16_t)g_text_line_height);
        if ((int16_t)(half_band + (int16_t)g_research_info_top_maybe) > y ||
            (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 0xc) == 0)
          info_text = 0x140;
        else
          info_text = 0x148;
      }
      g_info_descriptor_plus04 = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, info_text);
      g_info_arg_a_maybe = g_research_label_width_maybe;
      g_info_kind_maybe = 0xa;
    }
    (void)rt_call_far(MK_FP(SEG_SEL(24), 0x091e), FAR_CDECL, (const uint16_t[]){hwnd, (uint16_t)x, (uint16_t)y}, 3);
    FRAME_LEAVE();
    return 1;
  }
  /* an arrow button: repeat while the button is held */
  GetClientRect(hwnd, BP_FP(-0x12));
  (void)CALL_ORIG(8, 0x9aea, FAR_CDECL, 9, (uint16_t)(rt_bp - 0x2a), hwnd, 0, arrow_rect, frame_id, 0x50, 0, 0, 0);
  const int16_t step = (modifiers & 4) == 0 ? 1 : 10;
  while ((uint16_t)CALL_ORIG(8, 0x9b74, FAR_CDECL, 1, (uint16_t)(rt_bp - 0x2a)) != 0) {
    int16_t percent = (int16_t)((int16_t)(step * direction) + (int16_t)g_research_percent_edit_maybe);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if ((uint16_t)percent != g_research_percent_edit_maybe) {
      g_research_percent_edit_maybe = (uint16_t)percent;
      g_research_budget_maybe = rt_call_far(MK_FP(SEG_SEL(27), 0x492e), FAR_CDECL, (const uint16_t[]){(uint16_t)percent, (uint16_t)(percent < 0 ? 0xffff : 0)}, 2);
      const uint16_t repaint[4] = {hwnd, BP_LOCAL(uint16_t, -0x28), (uint16_t)(rt_bp - 0x12), 0x4200};
      (void)rt_call_far(MK_FP(SEG_SEL(27), 0x06ba), FAR_CDECL, repaint, 4);
    }
  }
  FRAME_LEAVE();
  return 1;
}

/* ---- 27:0x0000 ---------------------------------------------------------------------------------- */

/* RESEARCHDLG's WM_INITDIALOG: the dialog loads the player's research percent and field, sizes the
 * six radio buttons, the close and help buttons and the dialog itself to the text, fills the "next
 * field" combo box and works out the resources the research would spend. Uses the frame of RESEARCHDLG
 * (the rectangles at BP-0x20 and BP-0x18). */
static void research_dialog_init(uint16_t hwnd)
{
  g_research_percent_edit_maybe = (uint16_t)(int16_t)*player_research_percent(g_current_player);
  uint16_t field = *player_research_field_bits(g_current_player) & 0xf;
  g_research_field_maybe = field;
  CheckRadioButton(hwnd, CMD_RESEARCH_FIELD_FIRST, CMD_RESEARCH_FIELD_LAST, (int16_t)(field + CMD_RESEARCH_FIELD_FIRST));
  uint16_t hdc = GetDC(hwnd);
  uint16_t old_font = SelectObject(hdc, g_font_arial_bold_8pt);
  uint16_t title = (uint16_t)PENDING(1, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x1b8));
  int16_t title_width = (int16_t)(uint16_t)GetTextExtent(hdc, MK_FP(MEM_SEL_DGROUP, title), 7);
  g_research_radio_width_maybe = 0;
  for (int16_t id = CMD_RESEARCH_FIELD_FIRST; id <= CMD_RESEARCH_FIELD_LAST; id++) {
    uint16_t length = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(id - 0x3dd), DS_TEXT_BUFFER));
    int16_t width = (int16_t)(uint16_t)GetTextExtent(hdc, MK_FP(MEM_SEL_DGROUP, DS_TEXT_BUFFER), (int16_t)length);
    if ((int16_t)g_research_radio_width_maybe < width) g_research_radio_width_maybe = (uint16_t)width;
    uint16_t radio = GetDlgItem(hwnd, id);
    SetWindowText(radio, MK_FP(MEM_SEL_DGROUP, DS_TEXT_BUFFER));
    (void)SendMessage(radio, WM_SETFONT, g_font_arial_bold_8pt, 0);
  }
  g_research_radio_width_maybe = (uint16_t)(g_research_radio_width_maybe + 0x40);
  int16_t y = (int16_t)((uint16_t)(g_text_line_height << 2) - (uint16_t)((int16_t)g_text_line_height >> 1) + 1);
  for (int16_t id = CMD_RESEARCH_FIELD_FIRST; id <= CMD_RESEARCH_FIELD_LAST; id++) {
    (void)SetWindowPos(GetDlgItem(hwnd, id), 0, 0x10, y, (int16_t)g_research_radio_width_maybe,
                       three_halves((int16_t)g_text_line_height), SWP_NOZORDER | SWP_SHOWWINDOW);
    y = (int16_t)(y + three_halves((int16_t)g_text_line_height));
  }
  uint16_t combo = GetDlgItem(hwnd, CMD_RESEARCH_NEXT_COMBO);
  for (uint16_t k = 0; k <= 7; k++) {
    uint16_t text = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, (uint16_t)(0x53 + k)));
    (void)SendMessage(combo, CB_ADDSTRING, 0, MK_FP(MEM_SEL_DGROUP, text));
  }
  int16_t next_field = (int16_t)((int8_t)*player_research_field_bits(g_current_player) >> 4);
  if (next_field == 6) next_field = 0;
  else if (next_field < 6) next_field++;
  (void)SendMessage(combo, CB_SETCURSEL, (uint16_t)next_field, 0);
  g_research_column_x_maybe = (uint16_t)(title_width + (int16_t)g_research_radio_width_maybe + 0x28);
  g_research_label_width_maybe = 0;
  for (int16_t id = 0x4c; id <= 0x51; id++) {
    uint16_t length = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)id, DS_TEXT_BUFFER));
    int16_t width = (int16_t)(uint16_t)GetTextExtent(hdc, MK_FP(MEM_SEL_DGROUP, DS_TEXT_BUFFER), (int16_t)length);
    if ((int16_t)g_research_label_width_maybe < width) g_research_label_width_maybe = (uint16_t)width;
  }
  g_research_label_width_copy_maybe = g_research_label_width_maybe;
  uint16_t sample = (uint16_t)PENDING(1, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x37e));
  int16_t sample_width = (int16_t)(uint16_t)GetTextExtent(hdc, MK_FP(MEM_SEL_DGROUP, sample), 7);
  g_research_label_width_maybe = (uint16_t)(g_research_label_width_maybe + (uint16_t)(sample_width + 0x30));
  y = (int16_t)(y + (int16_t)((uint16_t)g_text_line_height * 10));
  const int16_t button_y = (int16_t)(y - three_halves((int16_t)g_text_line_height) - 8);
  (void)SetWindowPos(GetDlgItem(hwnd, CMD_RESEARCH_CLOSE), 0, (int16_t)(g_research_label_width_maybe + g_research_column_x_maybe - 0x98),
                     button_y, 0x46, three_halves((int16_t)g_text_line_height), SWP_NOZORDER | SWP_SHOWWINDOW);
  (void)SetWindowPos(GetDlgItem(hwnd, CMD_RESEARCH_HELP), 0, (int16_t)(g_research_label_width_maybe + g_research_column_x_maybe - 0x4c),
                     button_y, 0x46, three_halves((int16_t)g_text_line_height), SWP_NOZORDER | SWP_SHOWWINDOW);
  GetWindowRect(hwnd, BP_FP(-0x20));
  GetClientRect(hwnd, BP_FP(-0x18));
  (void)SetWindowPos(hwnd, 0, 0, 0,
                     (int16_t)(BP_LOCAL(int16_t, -0x1c) - BP_LOCAL(int16_t, -0x20) - BP_LOCAL(int16_t, -0x14) + (int16_t)g_research_label_width_maybe +
                               (int16_t)g_research_column_x_maybe),
                     (int16_t)(y - BP_LOCAL(int16_t, -0x1e) - BP_LOCAL(int16_t, -0x12) + BP_LOCAL(int16_t, -0x1a)), SWP_NOMOVE | SWP_NOZORDER);
  (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, DS_RESEARCH_DLG_POS, 1);
  (void)SelectObject(hdc, old_font);
  (void)ReleaseDC(hwnd, hdc);
  g_research_resources_total_maybe = 0;
  uint16_t planet_seg = g_planet_table_segment_maybe;
  uint16_t at = g_planet_table;
  uint16_t end = (uint16_t)(at + (uint16_t)(g_planet_count * sizeof(struct Planet)));
  if (end > at) {
    for (; at < end; at = (uint16_t)(at + sizeof(struct Planet))) {
      if (FAR_REC(struct Planet, planet_seg, at)->owner != g_current_player) continue;
      int32_t resources = (int16_t)(uint16_t)CALL_ORIG(9, 0x4de8, FAR_CDECL, 3, at, planet_seg, g_current_player);
      g_research_resources_total_maybe = (uint32_t)(g_research_resources_total_maybe + (uint32_t)resources);
    }
  }
  const uint16_t budget_args[2] = {g_research_percent_edit_maybe, (uint16_t)((int16_t)g_research_percent_edit_maybe < 0 ? 0xffff : 0)};
  g_research_budget_maybe = rt_call_far(MK_FP(SEG_SEL(27), 0x492e), FAR_CDECL, budget_args, 2);
  if ((g_ui_flags_b_maybe & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
}

/* RESEARCHDLG's WM_COMMAND (and, when the cursor is over nothing of interest, WM_SETCURSOR: the
 * original jumps into this code with the message's wParam and lParam). The radio buttons of the six
 * fields select the field and redraw the panel; the close button (id 2) stores a changed percent,
 * field or next field into the player's record and ends the dialog; the help button opens help topic
 * 0x42e. Returns the dialog procedure's result. Uses the frame of RESEARCHDLG. */
static uint16_t research_dialog_command(uint16_t hwnd, uint16_t id, uint32_t lParam)
{
  if ((uint16_t)(lParam >> 16) == 0 && id >= CMD_RESEARCH_FIELD_FIRST && id <= CMD_RESEARCH_FIELD_LAST) {
    if (IsDlgButtonChecked(hwnd, (int16_t)id) == 0) return 0;
    g_research_field_maybe = (uint16_t)(id - CMD_RESEARCH_FIELD_FIRST);
    GetClientRect(hwnd, BP_FP(-0x18));
    const uint16_t args[4] = {hwnd, 0, (uint16_t)(rt_bp - 0x18), 4};
    (void)rt_call_far(MK_FP(SEG_SEL(27), 0x06ba), FAR_CDECL, args, 4);
    return 0;
  }
  if (id == CMD_RESEARCH_CLOSE) {
    uint16_t changed = 0;
    int16_t next_field = (int16_t)(uint16_t)SendMessage(GetDlgItem(hwnd, CMD_RESEARCH_NEXT_COMBO), CB_GETCURSEL, 0, 0);
    if (next_field == 0) next_field = 6;
    else if (next_field <= 6) next_field--;
    uint8_t bits = *player_research_field_bits(g_current_player);
    if ((bits & 0xf) != g_research_field_maybe || (int16_t)((int8_t)bits >> 4) != next_field ||
        (int16_t)*player_research_percent(g_current_player) != (int16_t)g_research_percent_edit_maybe) {
      *player_research_percent(g_current_player) = (int8_t)g_research_percent_edit_maybe;
      bits = (uint8_t)((bits & 0xf0) | (uint8_t)g_research_field_maybe);
      *player_research_field_bits(g_current_player) = bits;
      bits = (uint8_t)((bits & 0xf) | (uint8_t)(next_field << 4));
      *player_research_field_bits(g_current_player) = bits;
      BP_LOCAL(uint16_t, -0xa) = (uint16_t)((uint16_t)(bits << 8) + g_research_percent_edit_maybe);
      (void)CALL_ORIG(9, 0x6372, FAR_CDECL, 3, 0x22, 2, (uint16_t)(rt_bp - 0xa));
      changed = 1;
      if ((g_ui_flags_b_maybe & 8) != 0 && g_current_player == 0) {
        g_frame_flags_a_maybe |= 4;
        (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
      }
    }
    (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, DS_RESEARCH_DLG_POS, 0);
    (void)EndDialog(hwnd, (int16_t)changed);
    g_research_percent_edit_maybe = 0xffff;
    if ((g_ui_flags_b_maybe & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    return 1;
  }
  if (id == CMD_RESEARCH_HELP) {
    (void)WinHelp(hwnd, MK_FP(MEM_SEL_DGROUP, g_help_file_name), 1, 0x42e);
    return 1;
  }
  return 0;
}

/* RESEARCHDLG: 27:0x0000 (Ghidra and export name RESEARCHDLG). 1580 bytes. ENTER 0x42,0; PUSH DI;
 * PUSH SI. FAR PASCAL dialog procedure (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]);
 * AX only. The research dialog: the research percent (arrow buttons beside it), the field being
 * researched (six radio buttons 0x431 to 0x436) and the field researched next (combo box 0x43b), with an
 * information panel that 27:0x06ba draws. Frame: the paint structure is at BP-0x40, the client rect at
 * BP-0x18, the window rect at BP-0x20, the cursor point at BP-4.
 *   WM_PAINT (0xf)        BeginPaint, 27:0x06ba(hwnd, dc, client rect, -1), EndPaint; returns 1.
 *   WM_ERASEBKGND (0x14)  fills the client rect with the button face brush DS:0x10; returns 1.
 *   WM_CTLCOLOR (0x19)    for one of the six radio buttons, or any control of type 6 (static text), sets the
 *                         background colour DS:0x253a and returns the brush DS:0x10; otherwise 0.
 *   WM_SETCURSOR (0x20)   over the list of tech items or the text band the cursor becomes DS:0x4ac8 and
 *                         the result is 1; elsewhere the message is handled like WM_COMMAND.
 *   WM_INITDIALOG (0x110) see research_dialog_init.   WM_COMMAND (0x111) see research_dialog_command.
 *   WM_LBUTTONDOWN (0x201), WM_LBUTTONDBLCLK (0x203)  27:0x134c(hwnd, x, y, wParam); its result.
 * Every other message returns 0. */
DECOMP(27, 0x0000, RESEARCHDLG, FAR_PASCAL)
uint16_t RESEARCHDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x42, 2);
  uint16_t result = 0;
  switch (msg) {
    case WM_COMMAND:
      result = research_dialog_command(hwnd, wParam, lParam);
      break;
    case WM_PAINT: {
      uint16_t hdc = BeginPaint(hwnd, BP_FP(-0x40));
      GetClientRect(hwnd, BP_FP(-0x18));
      const uint16_t args[4] = {hwnd, hdc, (uint16_t)(rt_bp - 0x18), 0xffff};
      (void)rt_call_far(MK_FP(SEG_SEL(27), 0x06ba), FAR_CDECL, args, 4);
      EndPaint(hwnd, BP_FP(-0x40));
      result = 1;
      break;
    }
    case WM_ERASEBKGND:
      GetClientRect(hwnd, BP_FP(-0x18));
      (void)FillRect(wParam, BP_FP(-0x18), g_brush_btnface);
      result = 1;
      break;
    case WM_CTLCOLOR: {
      int16_t id = CMD_RESEARCH_FIELD_FIRST;
      do {
        if (GetDlgItem(hwnd, id) == (uint16_t)lParam) break;
        id++;
      } while (id <= CMD_RESEARCH_FIELD_LAST);
      if (id <= CMD_RESEARCH_FIELD_LAST || (uint16_t)(lParam >> 16) == 6) {
        (void)SetBkColor(wParam, g_color_btnface);
        result = g_brush_btnface;
      }
      break;
    }
    case WM_SETCURSOR:
      GetCursorPos(BP_FP(-4));
      ScreenToClient(hwnd, BP_FP(-4));
      if (BP_LOCAL(int16_t, -4) > 0xc && (int16_t)(g_research_column_x_maybe - 0xc) > BP_LOCAL(int16_t, -4) &&
          BP_LOCAL(int16_t, -2) >= (int16_t)g_research_list_top_maybe &&
          (int16_t)((int16_t)g_research_item_count_maybe * (int16_t)g_text_line_height + (int16_t)g_research_list_top_maybe) > BP_LOCAL(int16_t, -2)) {
        (void)SetCursor(g_cursor_info_panel);
        result = 1;
      } else if ((int16_t)g_research_info_top_maybe != -1 && BP_LOCAL(int16_t, -2) >= (int16_t)g_research_info_top_maybe &&
                 (int16_t)(g_text_line_height + g_text_line_height + g_research_info_top_maybe) > BP_LOCAL(int16_t, -2) &&
                 BP_LOCAL(int16_t, -4) > (int16_t)g_research_column_x_maybe) {
        (void)SetCursor(g_cursor_info_panel);
        result = 1;
      } else {
        /* the original jumps into the WM_COMMAND code here, so this message is handled like a command */
        result = research_dialog_command(hwnd, wParam, lParam);
      }
      break;
    case WM_INITDIALOG:
      research_dialog_init(hwnd);
      result = 1;
      break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      const uint16_t args[4] = {hwnd, (uint16_t)lParam, (uint16_t)(lParam >> 16), wParam};
      result = (uint16_t)rt_call_far(MK_FP(SEG_SEL(27), 0x134c), FAR_CDECL, args, 4);
      break;
    }
    default:
      break;
  }
  FRAME_LEAVE();
  return result;
}

/* ---- 27:0x06ba ---------------------------------------------------------------------------------- */

/* paint_research_dialog: 27:0x06ba (Ghidra FUN_10d0_06ba). 3218 bytes. ENTER 0x52,0; PUSH DI; PUSH SI.
 * FAR CDECL (hwnd [BP+6], hdc [BP+8] (0: use GetDC / ReleaseDC), clip_rect [BP+0xA] a near pointer to the
 * client rect, what [BP+0xC] see PAINT_* above). No result.
 * Draws the information panel of the research dialog in the font DS:0x2930 on the button face
 * colour: (what & 3) a box with "Tech Levels" and the six levels of the current player; the box of the
 * items still to come (the list of tech items by the level they need: categories 1 to 9 of the
 * request kinds, 2:0x50f8 answering the level + 1 for item (kind, index); the first 8 are remembered
 * as requests at DS:0x4c52 for the click handler 27:0x134c); the costs of the next level of the current
 * field (27:0x1546 less the research already spent, and the turns it takes at the research budget
 * DS:0x26b2); the resources of all planets (DS:0x27b6), the player's research resources, the research
 * percent with its two arrow buttons (rects DS:0x29d6 and DS:0x29de) and the budget; and the two extra
 * lines for the traits 4 and 0xc. A low-byte request of 4 without a box bit only asks for the region to be
 * repainted (InvalidateRect). The region the lines go to is the rect at BP-0x22 (left, top, right,
 * bottom), moved down as lines are drawn; BP-0x50 holds a field name (a string loaded by 8:0x8dc0), BP-0x32 a
 * control rect and BP-0x2a the 8-byte tech item request handed to 2:0x50f8 (kind, index word, then the
 * record pointer 2:0x50f8 stores). The text and background colours and the font are put back at the end.
 * The player's field nibble (+0x39, low half) is set to the displayed field (DS:0x51b2) while the list of
 * items is built and restored afterwards. */
DECOMP(27, 0x06ba, paint_research_dialog, FAR_CDECL)
void paint_research_dialog(uint16_t hwnd, uint16_t hdc, uint16_t clip_rect, uint16_t what)
{
  FRAME_ENTER_REGS(0x52, 2);
#define BOX BP_LOCAL(RECT16, -0x22)
#define LINE ((int16_t)g_text_line_height)
  const uint8_t what_lo = (uint8_t)what, what_hi = (uint8_t)(what >> 8);
  bool own_dc = false;
  if (hdc == 0) {
    own_dc = true;
    hdc = GetDC(hwnd);
  }
  uint16_t old_font = SelectObject(hdc, g_font_arial_bold_8pt);
  uint32_t old_text_color = SetTextColor(hdc, 0);
  uint32_t old_bk_color = SetBkColor(hdc, g_color_btnface);
  if (what_lo != 0) {
    uint16_t title = (uint16_t)PENDING(1, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x1b8));
    int16_t title_width = text_width(hdc, title, 7);
    (void)SetRect(BP_FP(-0x22), 8, LINE, (int16_t)(g_research_column_x_maybe - 8), (int16_t)(13 * LINE));
    if ((what_lo & PAINT_LEVELS_BOX) != 0) {
      /* the box with the six tech levels */
      (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, (uint16_t)(rt_bp - 0x22), 0xffff);
      uint16_t length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x44, DS_TEXT_BUFFER);
      BP_LOCAL(uint16_t, -4) = length;
      text_out_ds(hdc, (int16_t)(BOX.left + 8), (int16_t)(BOX.top - (LINE >> 1)), DS_TEXT_BUFFER, (int16_t)length);
      BOX.top = (int16_t)(BOX.top + (LINE >> 1));
      uint16_t title_again = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x1b8));
      text_out_ds(hdc, (int16_t)(BOX.right - title_width - 8), BOX.top, title_again, 7);
      BOX.top = (int16_t)(BOX.top + LINE);
      uint16_t label = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x1b9));
      text_out_ds(hdc, (int16_t)(BOX.left + 0x1a), BOX.top, label, 0xe);
      int16_t level_x = (int16_t)(BOX.right - (title_width >> 1) - 8);
      BP_LOCAL(int16_t, -6) = level_x;
      (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, (uint16_t)level_x, (uint16_t)BOX.top, DS_STR_LEVEL, 5);
      (void)PatBlt(hdc, (int16_t)(BOX.left + 8), (int16_t)(BOX.top + LINE), (int16_t)(BOX.right - BOX.left - 0x10), 1, BLACKNESS);
      BOX.top = (int16_t)(BOX.top + (LINE >> 2) + LINE + 2);
      for (uint16_t field = 0; field < TECH_FIELD_COUNT; field++) {
        push_word((uint16_t)(int16_t)*player_tech_level(g_current_player, field));
        int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_a_maybe), MK_FP(rt_ss, rt_sp));
        pop_words(1);
        (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, (uint16_t)level_x, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written);
        BOX.top = (int16_t)(BOX.top + three_halves(LINE));
      }
    }
    (void)SetRect(BP_FP(-0x22), 8, (int16_t)(three_halves(LINE) + BOX.bottom), (int16_t)(g_research_column_x_maybe - 8),
                  (int16_t)(NEAR_AT(int16_t, (uint16_t)(clip_rect + 6)) - 8));
    if ((what_lo & PAINT_INVALIDATE_ONLY) != 0 && (what_lo & PAINT_LEVELS_BOX) == 0) {
      (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, (uint16_t)(rt_bp - 0x22), 0xfffa, (uint16_t)-(LINE >> 1));
      BOX.top = (int16_t)(BOX.top + (LINE >> 1));
      InvalidateRect(hwnd, BP_FP(-0x22), 1);
    } else {
      /* the box with the items still to come, by the level they need */
      (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, (uint16_t)(rt_bp - 0x22), 0xffff);
      uint16_t length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x45, DS_TEXT_BUFFER);
      text_out_ds(hdc, (int16_t)(BOX.left + 8), (int16_t)(BOX.top - (LINE >> 1)), DS_TEXT_BUFFER, (int16_t)length);
      const uint16_t displayed_field = *player_research_field_bits(g_current_player) & 0xf;
      g_research_item_count_maybe = 0;
      g_research_list_top_maybe = (uint16_t)(BOX.top + g_text_line_height);
      bool list_full = false;
      for (int16_t level = 1; level < 10 && !list_full; level++) {
        /* the items needing this level (the last group: this level and every higher one) */
        const int16_t first_level = level, last_level = (level == 9) ? 0x1a : level;
        if (level == 1) (void)SetTextColor(hdc, 0x00007f00);
        else if (level >= 2 && level <= 4) (void)SetTextColor(hdc, 0x007f0000);
        else (void)SetTextColor(hdc, 0);
        uint16_t bit = 1;
        *player_research_field_bits(g_current_player) =
            (uint8_t)((*player_research_field_bits(g_current_player) & 0xf0) | (uint8_t)g_research_field_maybe);
        while (bit != 0 && !list_full) {
          uint16_t index = 0;
          BP_LOCAL(uint16_t, -0x2a) = bit;
          for (;;) {
            BP_LOCAL(uint16_t, -0x28) = (uint16_t)((index & 0xff) | (BP_LOCAL(uint8_t, -0x27) << 8));
            const uint16_t lookup[1] = {(uint16_t)(rt_bp - 0x2a)};
            uint16_t needs = (uint16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, lookup, 1);
            if (needs == 0) break;
            if (BP_LOCAL(uint16_t, -0x2a) == 0x2000 &&
                (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 1) != 0) {
              uint8_t item = BP_LOCAL(uint8_t, -0x28);
              if (item == 8 || item == 0xc || item == 0x10) break;
            }
            int16_t needed_level = (int16_t)(needs - 1);
            if (needed_level >= first_level && needed_level <= last_level) {
              BOX.top = (int16_t)(BOX.top + LINE);
              if ((int16_t)(BOX.top + LINE) > BOX.bottom) {
                list_full = true;
                break;
              }
              uint32_t record = BP_LOCAL(uint32_t, -0x26);
              const uint8_t *name = (const uint8_t *)LIN(MK_FP(FP_SEG(record), (uint16_t)(FP_OFF(record) + 8)));
              uint16_t name_length = cstr_length(name);
              memcpy(LINN(DS_TEXT_BUFFER), name, (size_t)name_length + 1);
              text_out_ds(hdc, (int16_t)(BOX.left + 8), BOX.top, DS_TEXT_BUFFER, (int16_t)name_length);
              if ((int16_t)g_research_item_count_maybe < 8) {
                uint16_t at = (uint16_t)(g_research_item_count_maybe << 2);
                NEAR_AT(uint16_t, (uint16_t)(at + DS_RESEARCH_ITEM_REQUESTS)) = BP_LOCAL(uint16_t, -0x2a);
                NEAR_AT(uint16_t, (uint16_t)(at + DS_RESEARCH_ITEM_REQUESTS + 2)) = BP_LOCAL(uint16_t, -0x28);
                g_research_item_count_maybe = (uint16_t)(g_research_item_count_maybe + 1);
              }
            }
            index++;
          }
          if (list_full) break;
          bit = (uint16_t)(bit << 1);
        }
      }
      *player_research_field_bits(g_current_player) =
          (uint8_t)((*player_research_field_bits(g_current_player) & 0xf0) | (uint8_t)displayed_field);
      (void)SetTextColor(hdc, 0);
    }
  }
  /* the box with the cost of the next level, the resources and the research percent */
  (void)SetRect(BP_FP(-0x22), (int16_t)(g_research_column_x_maybe + 8), LINE,
                (int16_t)(g_research_label_width_maybe + g_research_column_x_maybe - 8), (int16_t)(LINE << 3));
  const int16_t value_x = (int16_t)(BOX.left + g_research_label_width_copy_maybe + 8);
  const int16_t value_x2 = (int16_t)(value_x + text_width(hdc, DS_STR_999999, 6));
  if ((what_lo & PAINT_INVALIDATE_ONLY) != 0 && (what_lo & PAINT_LEVELS_BOX) == 0) {
    (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, (uint16_t)(rt_bp - 0x22), 0xfffa, (uint16_t)-(LINE >> 1));
    BOX.top = (int16_t)(BOX.top + (LINE >> 1));
    InvalidateRect(hwnd, BP_FP(-0x22), 1);
  } else {
    if ((what_hi & (PAINT_COST_LINES >> 8)) != 0) {
      /* the cost of the next level of the displayed field, less the research already spent in it */
      uint32_t cost;
      if (*player_tech_level(g_current_player, g_research_field_maybe) >= TECH_LEVEL_MAX) {
        cost = 0xffffffffu;
      } else {
        const uint16_t cost_args[3] = {g_research_field_maybe,
                                       (uint16_t)(int16_t)(*player_tech_level(g_current_player, g_research_field_maybe) + 1),
                                       g_current_player};
        cost = rt_call_far(MK_FP(SEG_SEL(27), 0x1546), FAR_CDECL, cost_args, 3);
        uint32_t spent = *player_research_spent(g_current_player, g_research_field_maybe);
        if ((g_game_flags & GAME_FLAG_SLOW_TECH_MAYBE) != 0) spent = spent + spent;
        cost = cost - spent;
        if ((int16_t)(cost >> 16) < 0) cost = 0;
      }
      if ((what_hi & (PAINT_COST_NUMBERS >> 8)) != 0 && (what_lo & PAINT_BOX_ANY) == 0) {
        BOX.top = (int16_t)(BOX.top + three_halves(LINE) * 2 + LINE);
        uint16_t old_brush = SelectObject(hdc, g_brush_btnface);
        (void)PatBlt(hdc, value_x, BOX.top, (int16_t)(value_x2 - value_x), LINE, PATCOPY);
        (void)SelectObject(hdc, old_brush);
      } else {
        (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, (uint16_t)(rt_bp - 0x22), 0xffff);
        uint16_t length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x46, DS_TEXT_BUFFER);
        text_out_ds(hdc, (int16_t)(BOX.left + 8), (int16_t)(BOX.top - (LINE >> 1)), DS_TEXT_BUFFER, (int16_t)length);
        BOX.top = (int16_t)(BOX.top + LINE);
        (void)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, (uint16_t)(g_research_field_maybe + 0x54), (uint16_t)(rt_bp - 0x50));
        push_word((uint16_t)(int16_t)(*player_tech_level(g_current_player, g_research_field_maybe) + 1));
        push_word(rt_ss);
        push_word((uint16_t)(rt_bp - 0x50));
        {
          int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(DS_FMT_TECH_LEVEL), MK_FP(rt_ss, rt_sp));
          pop_words(3);
          (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written, 0);
        }
        BOX.top = (int16_t)(BOX.top + three_halves(LINE));
        push_dword(0);
        uint16_t label_a = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x4c);
        pop_words(2);
        (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label_a, 0, 0);
        push_dword(cost);
        int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_dword_maybe), MK_FP(rt_ss, rt_sp));
        pop_words(2);
        if (cost == 0xffffffffu) {
          uint16_t never = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x339));
          text_out_ds(hdc, value_x, BOX.top, never, 9);
        } else {
          BP_LOCAL(uint16_t, -4) = (uint16_t)written;
          text_out_ds(hdc, value_x, BOX.top, DS_TEXT_BUFFER, written);
        }
        BOX.top = (int16_t)(BOX.top + three_halves(LINE));
        push_dword(0);
        uint16_t label_b = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x4d);
        pop_words(2);
        (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label_b, 0, 0);
      }
      /* the turns the research of the next level takes at the budget */
      bool show_turns = false;
      if (cost == 0xffffffffu) {
        uint16_t never = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x339));
        text_out_ds(hdc, value_x, BOX.top, never, 9);
      } else if (cost == 0) {
        cost = 1;
        show_turns = true;
      } else if (g_research_budget_maybe == 0) {
        uint16_t none = (uint16_t)PENDING(3, CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x33a));
        text_out_ds(hdc, value_x, BOX.top, none, 0xc);
      } else {
        int32_t budget = (int32_t)g_research_budget_maybe;
        if ((uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 4) != 0) budget = budget - (budget >> 1);
        cost = (uint32_t)idiv32((int32_t)((uint32_t)budget + cost - 1), budget, 0x0e45);
        show_turns = true;
      }
      if (show_turns) {
        push_word(cost == 1 ? 0x20 : 0x73);
        push_dword(cost);
        uint16_t turns_format = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x33b);
        int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(turns_format), MK_FP(rt_ss, rt_sp));
        pop_words(3);
        text_out_ds(hdc, value_x, BOX.top, DS_TEXT_BUFFER, written);
      }
      /* the combo box of the next field sits under this text */
      push_dword(0);
      uint16_t next_label = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x52);
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)(value_x - 0x3c), (uint16_t)(BOX.top + LINE + 5), next_label, 0, 0);
      GetClientRect(GetDlgItem(hwnd, CMD_RESEARCH_NEXT_COMBO), BP_FP(-0x32));
      MapWindowPoints(GetDlgItem(hwnd, CMD_RESEARCH_NEXT_COMBO), hwnd, BP_FP(-0x32), 2);
      if ((int16_t)(value_x - BP_LOCAL(int16_t, -0x32)) != 0x3c) {
        (void)SetWindowPos(GetDlgItem(hwnd, CMD_RESEARCH_NEXT_COMBO), 0, (int16_t)(value_x - 0x3c), (int16_t)(BOX.top + LINE + 2),
                           (int16_t)(BOX.right - value_x + 0x32), (int16_t)(LINE * 9), SWP_NOREDRAW | SWP_NOZORDER);
      }
    }
    BOX.top = (int16_t)(three_halves(LINE) + BOX.bottom);
    BOX.bottom = (int16_t)((LINE << 3) + BOX.top);
    if ((what_hi & (PAINT_RESOURCES_BOX >> 8)) != 0) {
      (void)CALL_ORIG(8, 0x9958, FAR_CDECL, 3, hdc, (uint16_t)(rt_bp - 0x22), 0xffff);
      uint16_t length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x47, DS_TEXT_BUFFER);
      text_out_ds(hdc, (int16_t)(BOX.left + 8), (int16_t)(BOX.top - (LINE >> 1)), DS_TEXT_BUFFER, (int16_t)length);
    }
    BOX.top = (int16_t)(BOX.top + LINE);
    if ((what_hi & (PAINT_RESOURCES_TOTAL >> 8)) != 0) {
      push_dword(0);
      uint16_t label = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x4e);
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label, 0, 0);
      push_dword(g_research_resources_total_maybe);
      int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_dword_maybe), MK_FP(rt_ss, rt_sp));
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x2, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written, 0);
    }
    BOX.top = (int16_t)(BOX.top + three_halves(LINE));
    if ((what_hi & (PAINT_RESOURCES_SPENT >> 8)) != 0) {
      push_dword(0);
      uint16_t label = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x4f);
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label, 0, 0);
      push_dword(*(uint32_ua *)LINN((uint16_t)(player_row(g_current_player) + offsetof(struct PlayerResearch, research_resources_maybe))));
      int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_dword_maybe), MK_FP(rt_ss, rt_sp));
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x2, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written, 0);
    }
    BOX.top = (int16_t)(BOX.top + three_halves(LINE));
    int16_t percent_width = 0;
    if ((what_hi & (PAINT_PERCENT_LABEL >> 8)) != 0) {
      push_dword(0);
      uint16_t label = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x50);
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label, 0, 0);
    } else if ((what_hi & (PAINT_PERCENT_VALUE >> 8)) != 0 && (what_lo & PAINT_BOX_ANY) == 0) {
      percent_width = text_width(hdc, DS_STR_100, 3);
    }
    push_word(g_research_percent_edit_maybe);
    int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_a_maybe), MK_FP(rt_ss, rt_sp));
    pop_words(1);
    (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x2, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written, (uint16_t)percent_width);
    if (!((what_hi & (PAINT_PERCENT_VALUE >> 8)) != 0 && (what_lo & PAINT_BOX_ANY) == 0)) {
      /* the percent sign and the two arrow buttons beside it */
      int16_t sign_width = text_width(hdc, DS_STR_PERCENT_SIGN, 1);
      text_out_ds(hdc, value_x2, BOX.top, DS_STR_PERCENT_SIGN_2, 1);
      int16_t arrows_left = (int16_t)(sign_width + value_x2 + 4);
      RECT16 *up = (RECT16 *)LINN(DS_RESEARCH_UP_RECT);
      RECT16 *down = (RECT16 *)LINN(DS_RESEARCH_DOWN_RECT);
      up->left = arrows_left;
      up->top = (int16_t)(BOX.top - 4);
      up->right = (int16_t)(arrows_left + 0xf);
      up->bottom = (int16_t)((LINE >> 1) + BOX.top + 1);
      down->left = arrows_left;
      down->top = up->top;
      down->right = up->right;
      down->bottom = up->bottom;
      OffsetRect(ds_far(DS_RESEARCH_DOWN_RECT), 0, (int16_t)(up->bottom - up->top - 1));
      (void)CALL_ORIG(8, 0x9cbe, FAR_CDECL, 5, hdc, DS_RESEARCH_UP_RECT, 0xa0, 0, 0);
      (void)CALL_ORIG(8, 0x9cbe, FAR_CDECL, 5, hdc, DS_RESEARCH_DOWN_RECT, 0xa1, 0, 0);
    }
    BOX.top = (int16_t)(BOX.top + three_halves(LINE));
    if (!((what_hi & (PAINT_PERCENT_VALUE >> 8)) != 0 && (what_lo & PAINT_BOX_ANY) == 0)) {
      push_dword(0);
      uint16_t label = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, 0x51);
      pop_words(2);
      (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x, (uint16_t)BOX.top, label, 0, 0);
    }
    push_dword(g_research_budget_maybe);
    written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(g_fmt_ptr_dword_maybe), MK_FP(rt_ss, rt_sp));
    pop_words(2);
    (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)value_x2, (uint16_t)BOX.top, DS_TEXT_BUFFER, (uint16_t)written, (uint16_t)(value_x2 - value_x));
    if ((uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 4) == 0 &&
        (uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 0xc) == 0) {
      g_research_info_top_maybe = 0xffff;
    } else {
      /* the extra lines of the traits 4 and 0xc */
      uint16_t length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x4fb, (uint16_t)(rt_bp - 0x50));
      BP_LOCAL(uint16_t, -2) = length;
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      BOX.left = (int16_t)(g_research_column_x_maybe + 8);
      BOX.top = (int16_t)(BOX.top + LINE * 3);
      g_research_info_top_maybe = (uint16_t)BOX.top;
      int16_t name_width = (int16_t)(uint16_t)GetTextExtent(hdc, BP_FP(-0x50), (int16_t)BP_LOCAL(uint16_t, -2));
      if ((uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 4) != 0) {
        (void)TextOut(hdc, BOX.left, BOX.top, BP_FP(-0x50), (int16_t)BP_LOCAL(uint16_t, -2));
        uint16_t trait_length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x132, DS_TEXT_BUFFER);
        text_out_ds(hdc, (int16_t)(BOX.left + name_width), BOX.top, DS_TEXT_BUFFER, (int16_t)trait_length);
        BOX.top = (int16_t)(BOX.top + LINE + 2);
      }
      if ((uint16_t)CALL_ORIG(28, 0x2230, FAR_CDECL, 2, player_row(g_current_player), 0xc) != 0) {
        (void)TextOut(hdc, BOX.left, BOX.top, BP_FP(-0x50), (int16_t)BP_LOCAL(uint16_t, -2));
        uint16_t trait_length = (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, 0x13a, DS_TEXT_BUFFER);
        text_out_ds(hdc, (int16_t)(name_width + BOX.left), BOX.top, DS_TEXT_BUFFER, (int16_t)trait_length);
      }
    }
  }
  (void)SetBkColor(hdc, old_bk_color);
  (void)SetTextColor(hdc, old_text_color);
  (void)SelectObject(hdc, old_font);
  if (own_dc) (void)ReleaseDC(hwnd, hdc);
#undef LINE
#undef BOX
  FRAME_LEAVE();
}

/* ---- 27:0x1604 ---------------------------------------------------------------------------------- */

enum { BROWSER_STEP_LIMIT = 0x15e }; /* BROWSERDLG gives up stepping through the items after this many tries */

/* Forget the tech browser: save its position, clear DS:0x14e and DS:0xd3e and uncheck its entry (command
 * 0x100) in submenu 5 of the main window's menu. WM_DESTROY and the close button both do it. */
static void browser_forget_dialog(uint16_t hwnd)
{
  (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, DS_BROWSER_DLG_POS, 0);
  g_hwnd_modeless_dialog = 0;
  g_tech_pending_flag_maybe = 0;
  const uint16_t menu_args[2] = {g_main_window, 5};
  uint16_t menu = (uint16_t)rt_call_far(MK_FP(SEG_SEL(4), 0x3b30), FAR_CDECL, menu_args, 2);
  (void)CheckMenuItem(menu, 0x0100, 0);
}
/* Ask 2:0x50f8 about the tech item request DS:0x252a. */
static uint16_t check_tech_request(void)
{
  const uint16_t request[1] = {DS_TECH_REQUEST};
  return (uint16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, request, 1);
}
/* Repaint the item panel (the whole window, erasing). */
static void repaint_item_panel(void) { InvalidateRect(g_hwnd_modeless_list_maybe, 0, 1); }

/* BROWSERDLG's WM_COMMAND (see BROWSERDLG). The browser steps through the items with the request
 * DS:0x252a: the kind word and the item byte. The kinds are the words of the table at cs:0x062c
 * (index 1 to 16); the combo box's first entry means "all kinds". 2:0x50f8 answers 0 for an item that does
 * not exist, 1 for an item the player can use, 0xffff for one that is not allowed and anything else for the
 * level it needs; 27:0x4af6 tells the special items the player lacks. Returns the dialog result. */
static uint16_t browser_dialog_command(uint16_t hwnd, uint16_t id, uint16_t notification)
{
  if (id == CMD_RESEARCH_CLOSE) {
    browser_forget_dialog(hwnd);
    (void)EndDialog(hwnd, 1);
    if ((g_ui_flags_b_maybe & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
    return 1;
  }
  if (id == CMD_BROWSER_KIND_COMBO) {
    /* another kind chosen: the first item of it that fits (the first usable one when the check box is clear) */
    if (notification != 1) return 0;
    const bool unchecked = IsDlgButtonChecked(hwnd, CMD_BROWSER_ONLY_AVAILABLE) == 0;
    const uint32_t chosen = SendMessage(GetDlgItem(hwnd, CMD_BROWSER_KIND_COMBO), CB_GETCURSEL, 0, 0);
    if ((int16_t)(chosen >> 16) < 0) return 0;
    uint16_t entry = (uint16_t)chosen;
    if ((chosen >> 16) == 0 && entry < 1) entry = 1;
    g_tech_arg_mask_maybe = cs_word((uint16_t)(CS_TECH_KIND_TABLE + entry + entry));
    g_tech_arg_item_low_maybe = 0;
    for (;;) {
      uint16_t answer = check_tech_request();
      if (answer == 0) {
        g_tech_arg_record_maybe = 0;
        break;
      }
      if (answer == 1 || unchecked) break;
      g_tech_arg_item_low_maybe = (uint8_t)(g_tech_arg_item_low_maybe + 1);
    }
    repaint_item_panel();
    return 0;
  }
  if (id != CMD_BROWSER_NEXT && id != CMD_BROWSER_PREVIOUS) return 0;
  /* previous / next item */
  const uint16_t start_item = g_tech_arg_item_low_maybe;
  const bool all_kinds = SendMessage(GetDlgItem(hwnd, CMD_BROWSER_KIND_COMBO), CB_GETCURSEL, 0, 0) == 0;
  uint16_t start_kind = 0;
  for (uint16_t at = CS_TECH_KIND_TABLE; at < CS_TECH_KIND_TABLE_END; at = (uint16_t)(at + 2)) {
    if (cs_word(at) == g_tech_arg_mask_maybe) break;
    start_kind++;
  }
  uint16_t kind_pos = start_kind;
  const bool unchecked = IsDlgButtonChecked(hwnd, CMD_BROWSER_ONLY_AVAILABLE) == 0;
  const uint8_t step = (id == CMD_BROWSER_NEXT) ? 1 : 0xff;
  uint16_t tries = 0;
  uint16_t answer = 0;
  enum { STEP, RETRY, JUDGE, SETTLE, CLEAR } state = STEP;
  for (;;) {
    if (state == STEP) {
      g_tech_arg_item_low_maybe = (uint8_t)(step + g_tech_arg_item_low_maybe);
      state = (g_tech_arg_item_low_maybe == start_item && !all_kinds) ? SETTLE : RETRY;
    }
    if (state == RETRY) {
      uint16_t before = tries++;
      if ((int16_t)before > BROWSER_STEP_LIMIT) {
        state = CLEAR;
      } else {
        answer = check_tech_request();
        if (answer != 0) {
          state = JUDGE;
        } else if (id == CMD_BROWSER_NEXT && all_kinds) {
          kind_pos++;
          if ((int16_t)kind_pos >= 0x11) kind_pos = 1;
          g_tech_arg_mask_maybe = cs_word((uint16_t)(CS_TECH_KIND_TABLE + kind_pos + kind_pos));
          g_tech_arg_item_low_maybe = 0;
        } else if (g_tech_arg_item_low_maybe == 0 && !all_kinds) {
          g_tech_arg_record_maybe = 0;
          state = SETTLE;
        } else if (id == CMD_BROWSER_PREVIOUS && all_kinds && g_tech_arg_item_low_maybe > 0x64) {
          kind_pos--;
          if ((int16_t)kind_pos <= 0) kind_pos = 0x10;
          g_tech_arg_mask_maybe = cs_word((uint16_t)(CS_TECH_KIND_TABLE + kind_pos + kind_pos));
          g_tech_arg_item_low_maybe = (uint8_t)((g_tech_arg_item_low_maybe & 0x64) | 0x64);
        } else if (id == CMD_BROWSER_NEXT) {
          g_tech_arg_item_low_maybe = 0;
        } else {
          state = JUDGE;
        }
      }
    }
    if (state == JUDGE) {
      if (answer == 1) {
        state = SETTLE;
      } else if (answer == 0 || !unchecked) {
        state = STEP;
        continue;
      } else if (answer == 0xffff) {
        const uint16_t request[1] = {DS_TECH_REQUEST};
        state = ((uint16_t)rt_call_far(MK_FP(SEG_SEL(27), 0x4af6), FAR_CDECL, request, 1) != 0) ? STEP : SETTLE;
        if (state == STEP) continue;
      } else {
        state = SETTLE;
      }
    }
    if (state == SETTLE) {
      const bool same_place = g_tech_arg_item_low_maybe == start_item && start_kind == kind_pos;
      if (same_place && (answer == 1 || unchecked)) return 0;
      if (same_place && check_tech_request() == 1) return 0;
      if (same_place) g_tech_arg_record_maybe = 0;
      repaint_item_panel();
      return 0;
    }
    if (state == CLEAR) {
      g_tech_arg_record_maybe = 0;
      repaint_item_panel();
      return 0;
    }
  }
}

/* BROWSERDLG: 27:0x1604 (Ghidra and export name BROWSERDLG). 1636 bytes. ENTER 0x3c,0; PUSH DI;
 * PUSH SI. FAR PASCAL dialog procedure (hwnd [BP+0xE], msg [BP+0xC], wParam [BP+0xA], lParam [BP+6]);
 * AX only. The tech browser: a combo box of the tech item kinds (0x10b), a check box (0x10a), the buttons
 * 0x42e and 0x42f (previous, next) and 2 (close), and the item panel (window class "starsbrowser",
 * BROWSERWNDPROC) that shows the request DS:0x252a. Frame: the paint structure at BP-0x38, the client rect at
 * BP-0x18.
 *   WM_DESTROY (2)        browser_forget_dialog; returns 0.
 *   WM_PAINT (0xf)        BeginPaint / EndPaint only; returns 1.
 *   WM_ERASEBKGND (0x14)  fills the client rect with the button face brush DS:0x10; returns 1.
 *   WM_CTLCOLOR (0x19)    the check box and the combo box (or a control of type 6) get the background colour
 *                         DS:0x253a and the brush DS:0x10; others 0.
 *   WM_INITDIALOG (0x110) records the dialog in DS:0x14e, sizes it and its controls to the line height
 *                         DS:0x267a and the text, creates the item panel (window DS:0x5658), fills the combo box with
 *                         the 17 strings from 0x42e and selects the first; when DS:0xd3e is 0 starts at the request
 *                         (kind 8, item 0) and asks 2:0x50f8 about it; returns 1.
 *   WM_COMMAND (0x111)    see browser_dialog_command.
 * Every other message returns 0. */
DECOMP(27, 0x1604, BROWSERDLG, FAR_PASCAL)
uint16_t BROWSERDLG(uint16_t hwnd, uint16_t msg, uint16_t wParam, uint32_t lParam)
{
  FRAME_ENTER_REGS(0x3c, 2);
  uint16_t result = 0;
  switch (msg) {
    case WM_DESTROY:
      browser_forget_dialog(hwnd);
      break;
    case WM_PAINT:
      (void)BeginPaint(hwnd, BP_FP(-0x38));
      EndPaint(hwnd, BP_FP(-0x38));
      result = 1;
      break;
    case WM_ERASEBKGND:
      GetClientRect(hwnd, BP_FP(-0x18));
      (void)FillRect(wParam, BP_FP(-0x18), g_brush_btnface);
      result = 1;
      break;
    case WM_CTLCOLOR: {
      int16_t id = CMD_BROWSER_ONLY_AVAILABLE;
      do {
        if (GetDlgItem(hwnd, id) == (uint16_t)lParam) break;
        id++;
      } while (id <= CMD_BROWSER_KIND_COMBO);
      if (id <= CMD_BROWSER_KIND_COMBO || (uint16_t)(lParam >> 16) == 6) {
        (void)SetBkColor(wParam, g_color_btnface);
        result = g_brush_btnface;
      }
      break;
    }
    case WM_INITDIALOG: {
      g_hwnd_modeless_dialog = hwnd;
      const int16_t line = (int16_t)g_text_line_height;
      const int16_t wide = (line > 0xe) ? 0x28 : 0;
      /* the dialog: twice the frame and the picture width wide, high enough for the controls and the picture */
      int16_t width = (int16_t)(GetSystemMetrics(7) + 0xb3);
      width = (int16_t)(width + width + wide);
      int16_t height = (int16_t)(GetSystemMetrics(8) * 2);
      height = (int16_t)(height + GetSystemMetrics(4));
      height = (int16_t)(height + (int16_t)(line * 16 - line) + (int16_t)g_cell_height_10pt + 0x67);
      (void)SetWindowPos(hwnd, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
      (void)CALL_ORIG(8, 0x97a2, FAR_CDECL, 3, hwnd, DS_BROWSER_DLG_POS, 1);
      uint16_t hdc = GetDC(hwnd);
      uint16_t old_font = SelectObject(hdc, g_font_arial_bold_8pt);
      uint16_t combo = GetDlgItem(hwnd, CMD_BROWSER_KIND_COMBO);
      int16_t length = GetDlgItemText(hwnd, CMD_BROWSER_PREVIOUS, ds_far(DS_TEXT_BUFFER), 0x50);
      int16_t button_width = (int16_t)(text_width(hdc, DS_TEXT_BUFFER, length) + 0xe);
      (void)SetWindowPos(GetDlgItem(hwnd, CMD_BROWSER_PREVIOUS), 0, 6, 6, button_width, three_halves(line), SWP_NOZORDER);
      (void)SetWindowPos(GetDlgItem(hwnd, CMD_BROWSER_NEXT), 0, (int16_t)(wide - button_width + 0x15e), 6, button_width,
                         three_halves(line), SWP_NOZORDER);
      (void)SetWindowPos(combo, 0, (int16_t)(button_width + 0xc), 6, (int16_t)((0xa6 - button_width) * 2 + wide),
                         (int16_t)(line * 0x12), SWP_NOZORDER);
      const int16_t row_y = (int16_t)(three_halves(line) + (int16_t)((line + 8) * 12) + (int16_t)g_cell_height_10pt);
      (void)SetWindowPos(GetDlgItem(hwnd, CMD_RESEARCH_CLOSE), 0, (int16_t)(wide - button_width + 0x15e), row_y, button_width,
                         three_halves(line), SWP_NOZORDER);
      (void)SetWindowPos(GetDlgItem(hwnd, CMD_BROWSER_ONLY_AVAILABLE), 0, 6, row_y, (int16_t)(wide - button_width + 0x158),
                         three_halves(line), SWP_NOZORDER);
      (void)SelectObject(hdc, old_font);
      (void)ReleaseDC(hwnd, hdc);
      if (g_tech_pending_flag_maybe == 0) {
        g_tech_arg_mask_maybe = 8;
        g_tech_arg_item_low_maybe = 0;
      }
      (void)check_tech_request();
      g_hwnd_modeless_list_maybe =
          CreateWindow(ds_far(DS_CLASS_STARSBROWSER), 0, 0x50000000u, 6, (int16_t)(three_halves(line) + 0xc), (int16_t)(wide + 0x158),
                       (int16_t)(line * 12 + (int16_t)g_cell_height_10pt + 0x4e), hwnd, 0, g_app_instance, 0);
      for (uint16_t string_id = CMD_BROWSER_PREVIOUS; string_id < 0x43f; string_id++) {
        push_word(combo);
        push_dword(CB_ADDSTRING << 16);
        uint16_t text = (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, string_id);
        pop_words(3);
        (void)SendMessage(combo, CB_ADDSTRING, 0, ds_far(text));
      }
      (void)SendMessage(combo, CB_SETCURSEL, 0, 0);
      if ((g_ui_flags_b_maybe & 8) != 0) (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0);
      result = 1;
      break;
    }
    case WM_COMMAND:
      result = browser_dialog_command(hwnd, wParam, (uint16_t)(lParam >> 16));
      break;
    default:
      break;
  }
  FRAME_LEAVE();
  return result;
}

/* ---- 27:0x1e06 ---------------------------------------------------------------------------------- */

/* The tech item kinds (the request word's bit): the items 2:0x50f8 knows, named after their first item. */
enum {
  TECH_KIND_ENGINE = 0x0001,
  TECH_KIND_SCANNER = 0x0002,
  TECH_KIND_SHIELD = 0x0004,
  TECH_KIND_ARMOR = 0x0008,
  TECH_KIND_BEAM_WEAPON = 0x0010,
  TECH_KIND_TORPEDO = 0x0020,
  TECH_KIND_BOMB = 0x0040,
  TECH_KIND_MINING_ROBOT = 0x0080,
  TECH_KIND_MINE_LAYER = 0x0100,
  TECH_KIND_STARGATE_DRIVER = 0x0200, /* stargates and mass drivers */
  TECH_KIND_STARBASE_HULL = 0x0400,
  TECH_KIND_ELECTRICAL = 0x0800,      /* cloaks, jammers, computers, capacitors */
  TECH_KIND_MECHANICAL = 0x1000,      /* modules, cargo pods, fuel tanks, thrusters */
  TECH_KIND_TERRAFORMER = 0x2000,
  TECH_KIND_SHIP_HULL = 0x4000,
  TECH_KIND_PLANETARY = 0x8000,       /* planetary scanners and defences, the Genesis Device */
  DT_WORDBREAK_NOPREFIX = 0x0810,     /* DrawText: DT_WORDBREAK | DT_NOPREFIX */
  NO_DESCRIPTION = 0xffff
};
/* More DGROUP data of the panel. */
#define g_color_btntext_rgb (*(uint32_ua *)&g_color_btntext)           /* COLOR_BTNTEXT as a COLORREF (globals.h types its low word only) */
#define g_brush_chart_line_maybe g_DAT_1110_0022                 /* brush selected for the chart's horizontal 100% line */
#define g_brush_yellow_maybe g_DAT_1110_59e0                     /* brush that highlights the warp 10 column of the engine chart (the colour set with it is yellow) */
#define g_pen_chart_maybe g_DAT_1110_5752                        /* the pen of the engine and planetary charts' curves */
#define g_pen_default_maybe g_DAT_1110_59f8                      /* the pen selected back after a curve of the planetary chart */
#define g_font_note_small_maybe g_font_arial_7pt                 /* note text under the stats, line height 14 or less */
#define g_font_note_large_maybe g_font_arial_6pt                 /* ... and above 14 (DS:0x293a) */
#define g_palette_bitmaps_maybe g_DAT_1110_2808                  /* DS:0x2808: the picture bitmaps of the items, a word per sheet */
enum {
  DS_COST_COLORS = 0x0418, /* six COLORREF dwords: blue, green, yellow, white, red, black: the colours of the cost rows */
  DS_COST_LABELS = 0x0464  /* six near pointers to the cost labels: Ironium Boranium Germanium Colonists Fuel Resources */
};

/* The record of a tech item request: the request is a near pointer to {kind word, item byte, ..., record
 * far pointer at +4}; the record starts with the item's name at +8 and has its numbers from +0x32 on. */
static inline uint16_t request_kind(uint16_t req) { return NEAR_AT(uint16_ua, req); }
static inline uint8_t request_item(uint16_t req) { return NEAR_AT(uint8_t, (uint16_t)(req + 2)); }
static inline uint32_t request_record(uint16_t req) { return NEAR_AT(uint32_ua, (uint16_t)(req + 4)); }
static inline uint16_t record_word(uint16_t req, uint16_t disp) {
  uint32_t record = request_record(req);
  return *(uint16_ua *)LIN(MK_FP(FP_SEG(record), (uint16_t)(FP_OFF(record) + disp)));
}
static inline uint32_t record_far(uint16_t req, uint16_t disp) {
  uint32_t record = request_record(req);
  return MK_FP(FP_SEG(record), (uint16_t)(FP_OFF(record) + disp));
}
/* The string of the string table (a near pointer to it, 8:0x8df4) and the length of the string put at DS:buffer (8:0x8dc0). */
static inline uint16_t string_near(uint16_t id) { return (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id); }
static inline uint16_t string_load(uint16_t id, uint16_t buffer) { return (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, buffer); }
/* The text drawing helpers of segment 8 (their names are provisional: read from their uses here).
 *   8:0x9072 text centred on x       8:0x9398 text at (x, y), a length of 0 means the whole string
 *   8:0x90b0 text at *x, y, advancing *x   8:0x90fe wrapped text in a column   8:0x96e6 shrink a rect
 *   8:0x9482 text centred in a rect   8:0x9958 / 8:0x9cbe draw a box / a button frame */
static inline void draw_text_centered(uint16_t hdc, int16_t x, int16_t y, uint16_t text, uint16_t length) {
  (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, (uint16_t)x, (uint16_t)y, text, length);
}
static inline void draw_text_at(uint16_t hdc, int16_t x, int16_t y, uint16_t text, uint16_t length, uint16_t flag) {
  (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)x, (uint16_t)y, text, length, flag);
}
static inline void draw_text_run(uint16_t hdc, uint16_t x_ptr, int16_t y, uint16_t text, uint16_t length, uint16_t flag) {
  (void)CALL_ORIG(8, 0x90b0, FAR_CDECL, 6, hdc, x_ptr, (uint16_t)y, text, length, flag);
}
static inline void draw_text_flow(uint16_t hdc, uint16_t x_ptr, uint16_t y_ptr, uint16_t text, uint16_t length, int16_t left, int16_t width,
                                  uint16_t flag_lo, uint16_t flag_hi) {
  (void)CALL_ORIG(8, 0x90fe, FAR_CDECL, 10, hdc, x_ptr, y_ptr, text, length, (uint16_t)left, (uint16_t)width, 0, flag_lo, flag_hi);
}
static inline void shrink_rect(uint16_t rect_ptr, int16_t dx, int16_t dy) {
  (void)CALL_ORIG(8, 0x96e6, FAR_CDECL, 3, rect_ptr, (uint16_t)dx, (uint16_t)dy);
}

#define PANEL BP_LOCAL(RECT16, -0x34)             /* the text box the lines of the item go in (left, top, right, bottom) */
#define DESCRIPTION BP_LOCAL(uint16_t, -0x20)     /* string id of the note under the stats (NO_DESCRIPTION: none) */
#define LINE ((int16_t)g_text_line_height)

/* The text of a string put in the text buffer with one formatted number: _wsprintf(DS:0x5866, DS:fmt, value). */
static inline uint16_t format_number(uint16_t format, uint16_t value)
{
  push_word(value);
  int16_t length = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(1);
  return (uint16_t)length;
}

/* Engines: a chart of the fuel usage at each warp speed (the words from record+0x36, one per warp). */
static void paint_engine_item(uint16_t hdc, uint16_t req)
{
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  PANEL.right = (int16_t)(PANEL.right - 2);
  uint16_t length = string_load(0x27, DS_TEXT_BUFFER);
  draw_text_centered(hdc, (int16_t)(((int16_t)(PANEL.right - PANEL.left) >> 1) + PANEL.left), PANEL.top, DS_TEXT_BUFFER, length);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  uint16_t warp_text = (uint16_t)PENDING(1, string_near(0x344));
  const int16_t warp_width = text_width(hdc, warp_text, 4);
  const int16_t column_width = (int16_t)((int16_t)(PANEL.right - PANEL.left - warp_width - 8) / 10);
  const int16_t plot_height = (int16_t)(PANEL.bottom - PANEL.top - LINE);
  const int16_t row_height = (int16_t)((int16_t)(plot_height - 8) / 6);
  (void)PatBlt(hdc, (int16_t)(PANEL.left + warp_width + 6), PANEL.top, 1, (int16_t)(plot_height - 4), BLACKNESS);
  (void)PatBlt(hdc, (int16_t)(PANEL.left + warp_width + 6), (int16_t)(PANEL.bottom - LINE - 4),
               (int16_t)(PANEL.right - PANEL.left - warp_width - 6), 1, BLACKNESS);
  int16_t origin_x = (int16_t)(PANEL.left + warp_width);
  int16_t line_y = (int16_t)(PANEL.bottom - 6 * row_height - LINE - 4);
  (void)SetTextColor(hdc, 0x7f);
  /* the percent scale on the left: 800, 400, 200, 100 (a line across), 50 and 25 */
  int16_t y = line_y;
  int16_t percent = 0x320;
  do {
    if (percent == 0x64) {
      (void)SetTextColor(hdc, 0);
      (void)SelectObject(hdc, g_brush_chart_line_maybe);
      (void)PatBlt(hdc, (int16_t)(origin_x + 4), y, (int16_t)(PANEL.right - warp_width - PANEL.left - 4), 1, PATCOPY);
    }
    uint16_t written = format_number(g_fmt_percent, (uint16_t)percent);
    draw_text_at(hdc, origin_x, (int16_t)(y - (LINE >> 1)), DS_TEXT_BUFFER, written, 0);
    (void)PatBlt(hdc, (int16_t)(origin_x + 4), y, 5, 1, BLACKNESS);
    y = (int16_t)(y + row_height);
    percent = (int16_t)(percent >> 1);
  } while (percent >= 0x19);
  /* the warp numbers under the chart */
  const int16_t label_y = (int16_t)(PANEL.bottom - LINE);
  uint16_t warp_label = (uint16_t)PENDING(2, string_near(0x344));
  draw_text_at(hdc, origin_x, label_y, warp_label, 4, 0);
  origin_x = (int16_t)(origin_x + 6);
  const int16_t axis_y = (int16_t)(PANEL.bottom - LINE - 4);
  const uint16_t half_digit = (uint16_t)text_width(hdc, DS_STR_0, 1) >> 1;
  int16_t digit_count = 1;
  BP_LOCAL(uint8_t, -0x28) = 0x30;
  const uint32_t saved_color = SetTextColor(hdc, g_color_btntext_rgb);
  const uint8_t item = request_item(req);
  /* the engines that are safe at warp 10 (items 7, 8, 9, 14 and 15) */
  const bool safe_at_warp_10 = item == 7 || item == 9 || item == 0xe || item == 0xf || item == 8;
  for (int16_t step = 0; step <= 0x14; step = (int16_t)(step + 2)) {
    if (step == 0x14) {
      digit_count = 2;
      BP_LOCAL(uint8_t, -0x28) = 0x31;
      BP_LOCAL(uint8_t, -0x27) = 0x30;
      if (!safe_at_warp_10) {
        /* an unsafe warp 10 is marked on a yellow band */
        uint16_t old_brush = SelectObject(hdc, g_brush_yellow_maybe);
        (void)SetTextColor(hdc, 0xffff);
        uint32_t old_background = SetBkColor(hdc, g_color_btnface);
        (void)PatBlt(hdc, (int16_t)(origin_x - column_width + 1), PANEL.top, column_width, (int16_t)(axis_y - PANEL.top), PATCOPY);
        (void)SetBkColor(hdc, old_background);
        (void)SelectObject(hdc, old_brush);
      }
    }
    const int16_t usage = (int16_t)record_word(req, (uint16_t)(0x36 + step));
    const int16_t usage_next = (int16_t)record_word(req, (uint16_t)(0x38 + step));
    uint32_t colour = g_color_btntext_rgb;
    if (usage <= 0x78) {
      if ((step == 0x14 && safe_at_warp_10) || usage_next > 0x78 || (step == 0x12 && !safe_at_warp_10)) colour = 0x007f0000;
    }
    (void)SetTextColor(hdc, colour);
    (void)TextOut(hdc, (int16_t)(origin_x - digit_count * (int16_t)half_digit), label_y, BP_FP(-0x28), digit_count);
    if (step > 0) (void)PatBlt(hdc, origin_x, (int16_t)(label_y - 6), 1, 5, BLACKNESS);
    BP_LOCAL(uint8_t, -0x28) = (uint8_t)(BP_LOCAL(uint8_t, -0x28) + 1);
    origin_x = (int16_t)(origin_x + column_width);
  }
  (void)SetTextColor(hdc, saved_color);
  const uint16_t old_pen = SelectObject(hdc, g_pen_chart_maybe);
  /* the curve: one point per warp */
  origin_x = (int16_t)(warp_width + PANEL.left + 6);
  for (int16_t step = 0; step <= 0x14; step = (int16_t)(step + 2)) {
    int16_t scale = 0x19;
    int16_t curve_y = axis_y;
    const int16_t usage = (int16_t)record_word(req, (uint16_t)(0x36 + step));
    if (usage >= 0x19) {
      do {
        curve_y = (int16_t)(curve_y - row_height);
        scale = (int16_t)(scale + scale);
      } while (scale <= usage);
    }
    if (scale == 0x19) {
      if (usage == 0) {
        curve_y = (int16_t)(curve_y - 1);
      } else {
        int16_t rise = (int16_t)((int16_t)((uint16_t)(usage * row_height) << 2) / 100);
        if (rise < 1) rise = 1;
        curve_y = (int16_t)(curve_y - rise);
      }
    } else {
      const int16_t half = (int16_t)(scale >> 1);
      int16_t ratio = MulDiv((int16_t)(usage - half), 100, half);
      curve_y = (int16_t)(curve_y + (int16_t)((int16_t)(ratio * row_height) / -100));
    }
    if (step == 0) (void)MoveTo(hdc, origin_x, curve_y);
    else (void)LineTo(hdc, origin_x, curve_y);
    origin_x = (int16_t)(origin_x + column_width);
  }
  (void)SelectObject(hdc, old_pen);
  const uint16_t kind_of_engine = record_word(req, 0x34);
  const uint16_t which = (uint16_t)(kind_of_engine - 1);
  if (which <= 5) {
    static const uint16_t notes[6] = {0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x394};
    DESCRIPTION = notes[which];
  } else {
    uint8_t unknown_item = request_item(req);
    if (unknown_item >= 0xb && unknown_item <= 0xe) DESCRIPTION = 0x188;
  }
}

/* Scanners: the text of the detection range and of the planet / cargo abilities. */
static void paint_scanner_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_8pt);
  const uint16_t range = record_word(req, 0x34);
  uint16_t length;
  if (range == 0) {
    length = string_load(0x32, DS_TEXT_BUFFER);
  } else {
    (void)string_load(0x31, (uint16_t)(rt_bp - 0x136));
    push_word(range);
    length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
    pop_words(1);
  }
  int16_t height = DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
  PANEL.top = (int16_t)(PANEL.top + (LINE >> 1) + height);
  const uint16_t penetration = record_word(req, 0x36);
  uint16_t description = DESCRIPTION;
  if (penetration == 0) {
    length = string_load(0x33, DS_TEXT_BUFFER);
  } else if (penetration == 4) {
    const uint8_t item = request_item(req);
    length = string_load(item == 5 ? 0x36 : item == 0xe ? 0x37 : 0x35, DS_TEXT_BUFFER);
    description = 0x38;
  } else {
    (void)string_load(0x34, (uint16_t)(rt_bp - 0x136));
    push_word(penetration == 1 ? 0x32 : penetration == 2 ? 0x64 : 0xc8);
    length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
    pop_words(1);
    description = 0x160;
  }
  (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
  DESCRIPTION = description;
}

/* Shields and armor: "<label> <strength>" on one line (label_id: string 0x345 or 0x346), then a note for some
 * items. The three notes: item (string note, description id). */
static void paint_shield_armor_item(uint16_t hdc, uint16_t req, uint16_t label_id, bool shield)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -4) = PANEL.left;
  uint16_t label = (uint16_t)PENDING(2, string_near(label_id));
  draw_text_run(hdc, (uint16_t)(rt_bp - 4), PANEL.top, label, 0, 1);
  (void)SelectObject(hdc, g_font_arial_8pt);
  uint16_t length = format_number(g_fmt_ptr_a_maybe, record_word(req, 0x34));
  draw_text_run(hdc, (uint16_t)(rt_bp - 4), PANEL.top, DS_TEXT_BUFFER, length, 1);
  const uint8_t item = request_item(req);
  uint16_t note_id;
  uint16_t note_text;
  if (shield) {
    if (item == 4) { DESCRIPTION = 0x3a; note_text = 0x3c; }
    else if (item == 3) { DESCRIPTION = 0x3b; note_text = 0x3f; }
    else if (item == 6) { DESCRIPTION = 0x391; note_text = 0x40; }
    else return;
    note_id = note_text;
    length = string_load(note_id, DS_TEXT_BUFFER);
  } else {
    if (item == 7) {
      DESCRIPTION = 0x3a;
      length = string_load(0x3d, DS_TEXT_BUFFER);
    } else if (item == 6) {
      DESCRIPTION = 0x4c5;
      length = string_load(0x3e, DS_TEXT_BUFFER);
    } else if (item == 9) {
      DESCRIPTION = 0x391;
      length = string_load(0x395, DS_TEXT_BUFFER);
      PANEL.top = (int16_t)(PANEL.top + LINE + 4);
      (void)SelectObject(hdc, g_font_arial_8pt);
      (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
      return;
    } else {
      return;
    }
  }
  PANEL.top = (int16_t)(PANEL.top + LINE + 4);
  (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
}

/* One "label: number" line of the weapon stats (beam weapons and torpedoes): the bold label (string
 * `label_id`) at (x, y), the value from record word `disp` after it. */
static void paint_stat_line(uint16_t hdc, uint16_t req, int16_t x, int16_t y, uint16_t label_id, uint16_t disp)
{
  uint16_t length = string_load(label_id, DS_TEXT_BUFFER);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  draw_text_at(hdc, x, y, DS_TEXT_BUFFER, length, 0);
  (void)SelectObject(hdc, g_font_arial_8pt);
  uint16_t written = format_number(g_fmt_ptr_a_maybe, record_word(req, disp));
  (void)TextOut(hdc, x, y, ds_far(DS_TEXT_BUFFER), (int16_t)written);
}

/* Beam weapons: power, range and initiative, then the notes of the weapon (bits of the flags word at +0x3a). */
static void paint_beam_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  const int16_t width = (int16_t)(PANEL.right - PANEL.left);
  const int16_t x = (int16_t)(((int16_t)(width + (width < 0 ? 3 : 0)) >> 2) + PANEL.left);
  int16_t y = PANEL.top;
  paint_stat_line(hdc, req, x, y, 0x347, 0x36);
  y = (int16_t)(y + LINE);
  paint_stat_line(hdc, req, x, y, 0x348, 0x34);
  y = (int16_t)(y + LINE);
  paint_stat_line(hdc, req, x, y, 0x4b, 0x38);
  y = (int16_t)(y + three_halves(LINE));
  PANEL.top = y;
  const uint16_t flags = record_word(req, 0x3a);
  if (flags != 0) {
    (void)SelectObject(hdc, g_font_note_small_maybe);
    if ((flags & 1) != 0) {
      uint16_t length = (uint16_t)PENDING(3, string_load(0x4e5, DS_TEXT_BUFFER));
      PANEL.top = (int16_t)(PANEL.top + DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX));
    }
    if ((flags & 2) != 0) {
      uint16_t length = (uint16_t)PENDING(3, string_load(0x4e6, DS_TEXT_BUFFER));
      PANEL.top = (int16_t)(PANEL.top + DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX));
      /* "sweeps N mines per year": three words are pending (the DrawText arguments) when the format is looked up */
      push_word(hdc);
      push_word(MEM_SEL_DGROUP);
      push_word(DS_TEXT_BUFFER);
      push_word((uint16_t)(record_word(req, 0x36) << 4));
      uint16_t format = string_near(0x4e7);
      int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(4);
      (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), written, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
    }
    (void)SelectObject(hdc, g_font_arial_8pt);
  } else if (request_item(req) == 0x12) {
    DESCRIPTION = 0x391;
    uint16_t length = string_load(0x396, DS_TEXT_BUFFER);
    (void)SelectObject(hdc, g_font_note_small_maybe);
    PANEL.top = (int16_t)(PANEL.top + DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX));
    length = (uint16_t)PENDING(3, string_load(0x397, DS_TEXT_BUFFER));
    (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
  }
  const uint8_t item = request_item(req);
  if (item == 2) DESCRIPTION = 0x151;
  else if (item == 0xe || item == 0x10) DESCRIPTION = 0x152;
}

/* Torpedoes: power, range, initiative and accuracy. */
static void paint_torpedo_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  const int16_t width = (int16_t)(PANEL.right - PANEL.left);
  const int16_t x = (int16_t)(((int16_t)(width + (width < 0 ? 3 : 0)) >> 2) + PANEL.left);
  int16_t y = PANEL.top;
  paint_stat_line(hdc, req, x, y, 0x347, 0x36);
  y = (int16_t)(y + LINE);
  paint_stat_line(hdc, req, x, y, 0x348, 0x34);
  y = (int16_t)(y + LINE);
  paint_stat_line(hdc, req, x, y, 0x4b, 0x38);
  y = (int16_t)(y + LINE);
  paint_stat_line(hdc, req, x, y, 0x349, 0x3a);
  const uint8_t item = request_item(req);
  if (item == 7) DESCRIPTION = 0x391;
  else if (item >= 8 && item <= 0xb) DESCRIPTION = 0x51a;
}

/* The note of a part, set in the column of the panel by 8:0x90fe and the cursor moved two lines down (the
 * tail several kinds share). `length` is the length of the text in the text buffer. */
static void paint_flow_note(uint16_t hdc, uint16_t length)
{
  BP_LOCAL(uint16_t, -0x14) = length;
  draw_text_flow(hdc, (uint16_t)(rt_bp - 0x2a), (uint16_t)(rt_bp - 0x26), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = (int16_t)(BP_LOCAL(int16_t, -0x26) + LINE + LINE);
}

/* Bombs: how many colonists and installations a bomb destroys a year. */
static void paint_bomb_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -2) = PANEL.left;
  BP_LOCAL(int16_t, -4) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  const uint8_t item = request_item(req);
  uint16_t length;
  if (item == 9) {
    length = string_load(0x15d, DS_TEXT_BUFFER);
  } else if (record_word(req, 0x36) == 0) {
    length = string_load(0x1bb, DS_TEXT_BUFFER);
  } else {
    /* "kills about <n>.<m>% of the colonists a year", then the minimum kill of the bomb class */
    (void)string_load(0x1bc, DS_TEXT_BUFFER);
    const int16_t kill_tenths = (int16_t)record_word(req, 0x36);
    push_word((uint16_t)(kill_tenths % 10));
    push_word((uint16_t)(kill_tenths / 10));
    length = (uint16_t)_wsprintf(BP_FP(-0x104), ds_far(DS_TEXT_BUFFER), MK_FP(rt_ss, rt_sp));
    pop_words(2);
    int16_t minimum = 0;
    if (item <= 4) minimum = 3;
    else if (item >= 0xa && item <= 0xe) minimum = 0x3e7;
    if (minimum > 0) {
      (void)string_load(minimum == 0x3e7 ? 0x504 : 0x1bd, DS_TEXT_BUFFER);
      push_word((uint16_t)(minimum * 100));
      length = (uint16_t)(length + (uint16_t)_wsprintf(MK_FP(rt_ss, (uint16_t)(rt_bp - 0x104 + length)), ds_far(DS_TEXT_BUFFER), MK_FP(rt_ss, rt_sp)));
      pop_words(1);
    }
    memcpy(LINN(DS_TEXT_BUFFER), LIN(BP_FP(-0x104)), (size_t)cstr_length((const uint8_t *)LIN(BP_FP(-0x104))) + 1);
  }
  BP_LOCAL(uint16_t, -0x14) = length;
  draw_text_flow(hdc, (uint16_t)(rt_bp - 2), (uint16_t)(rt_bp - 4), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
  BP_LOCAL(int16_t, -2) = PANEL.left;
  BP_LOCAL(int16_t, -4) = (int16_t)(BP_LOCAL(int16_t, -4) + (LINE >> 1));
  if (request_item(req) == 9) {
    DESCRIPTION = 0x15b;
    return;
  }
  /* what the bomb destroys: installations a year */
  const uint16_t installations = record_word(req, 0x38);
  if (installations == 0) {
    length = string_load(0x1be, DS_TEXT_BUFFER);
  } else {
    (void)string_load(0x1bf, (uint16_t)(rt_bp - 0x104));
    push_word(installations);
    length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x104), MK_FP(rt_ss, rt_sp));
    pop_words(1);
  }
  BP_LOCAL(uint16_t, -0x14) = length;
  draw_text_flow(hdc, (uint16_t)(rt_bp - 2), (uint16_t)(rt_bp - 4), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 1, 1);
  const uint8_t item2 = request_item(req);
  if (item2 >= 0xa && item2 <= 0xe) DESCRIPTION = 0x41;
  else if (item2 == 8) DESCRIPTION = 0x391;
}

/* Mining robots: the module description ("mines up to <n> kT of each mineral ...") built from three strings. */
static void paint_mining_robot_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  const uint8_t item = request_item(req);
  if (item > 7) return;
  if (item == 7) {
    DESCRIPTION = 0x15b;
    paint_flow_note(hdc, string_load(0x15c, DS_TEXT_BUFFER));
    return;
  }
  if (item == 0 || item == 5) DESCRIPTION = 0x14a;
  if (item != 1 && item != 6 && DESCRIPTION == NO_DESCRIPTION) DESCRIPTION = 0x14c;
  uint16_t length = string_load(0xdb, DS_TEXT_BUFFER);
  push_word(record_word(req, 0x34));
  length = (uint16_t)(length + (uint16_t)_wsprintf(ds_far((uint16_t)(DS_TEXT_BUFFER + length)), ds_far(g_fmt_ptr_a_maybe), MK_FP(rt_ss, rt_sp)));
  pop_words(1);
  length = (uint16_t)(length + string_load(0xdc, (uint16_t)(DS_TEXT_BUFFER + length)));
  BP_LOCAL(uint16_t, -0x14) = length;
  if (request_item(req) == 6) {
    DESCRIPTION = 0x391;
    length = (uint16_t)(length + string_load(0xdd, (uint16_t)(DS_TEXT_BUFFER + length)));
    BP_LOCAL(uint16_t, -0x14) = length;
  }
  paint_flow_note(hdc, length);
}

/* Mine layers: five lines (mines laid a year, safe speed, chance of a hit, damage, minimum damage), the
 * labels bold on the left (the widest one sets the column) and the numbers after them. */
static void paint_mine_layer_item(uint16_t hdc, uint16_t req)
{
  int16_t label_width = -1;
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  PANEL.top = (int16_t)(PANEL.top + LINE * 5);
  for (int16_t k = 4; k >= 0; k--) {
    PANEL.top = (int16_t)(PANEL.top - LINE);
    uint16_t length = string_load((uint16_t)(0x2c6 + k), DS_TEXT_BUFFER);
    BP_LOCAL(uint16_t, -0x14) = length;
    if (label_width == -1) label_width = text_width(hdc, DS_TEXT_BUFFER, (int16_t)length);
    draw_text_at(hdc, (int16_t)(PANEL.left + label_width), PANEL.top, DS_TEXT_BUFFER, length, 0);
  }
  (void)SelectObject(hdc, g_font_arial_8pt);
  /* the values of the mine class: BP-6 safe warp, BP-4 hit chance in tenths of a percent, BP-8 and BP-0xa damage to a ship
   * (without and with ram scoops), BP-0xc and BP-0x12 the minimum damage to a fleet */
  const uint8_t item = request_item(req);
  if (item <= 3) {
    BP_LOCAL(int16_t, -6) = 4;
    BP_LOCAL(int16_t, -4) = 3;
    BP_LOCAL(int16_t, -8) = 0x64;
    BP_LOCAL(int16_t, -0xa) = 0x7d;
    BP_LOCAL(int16_t, -0xc) = 0x1f4;
    BP_LOCAL(int16_t, -0x12) = 0x258;
    DESCRIPTION = (item == 1) ? 0x505 : 0x14d;
  } else if (item <= 6) {
    BP_LOCAL(int16_t, -6) = 6;
    BP_LOCAL(int16_t, -4) = 10;
    BP_LOCAL(int16_t, -8) = 0x1f4;
    BP_LOCAL(int16_t, -0xa) = 0x258;
    BP_LOCAL(int16_t, -0xc) = 0x7d0;
    BP_LOCAL(int16_t, -0x12) = 0x9c4;
    DESCRIPTION = 0x14d;
  } else if (item <= 9) {
    BP_LOCAL(int16_t, -6) = 5;
    BP_LOCAL(int16_t, -4) = 0x23;
    BP_LOCAL(int16_t, -0xa) = 0;
    BP_LOCAL(int16_t, -8) = 0;
    BP_LOCAL(int16_t, -0xc) = 0;
    BP_LOCAL(int16_t, -0x12) = 0;
    DESCRIPTION = (item == 7) ? 0x14e : 0x14d;
  }
  const int16_t column = (int16_t)(label_width + PANEL.left + 4);
  const int16_t mines = (int16_t)(record_word(req, 0x34) * 10);
  uint16_t written = format_number(g_fmt_ptr_a_maybe, (uint16_t)mines);
  (void)TextOut(hdc, column, PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)written);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  push_word(BP_LOCAL(uint16_t, -6));
  uint16_t format = string_near(0x359);
  written = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(1);
  (void)TextOut(hdc, column, PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)written);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  push_word((uint16_t)(BP_LOCAL(int16_t, -4) % 10));
  push_word((uint16_t)(BP_LOCAL(int16_t, -4) / 10));
  format = string_near(0x37f);
  written = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(2);
  (void)TextOut(hdc, column, PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)written);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  push_word(BP_LOCAL(uint16_t, -0xa));
  push_word(BP_LOCAL(uint16_t, -8));
  format = string_near(0x381);
  written = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(2);
  (void)TextOut(hdc, column, PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)written);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  push_word(BP_LOCAL(uint16_t, -0x12));
  push_word(BP_LOCAL(uint16_t, -0xc));
  format = string_near(0x380);
  written = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(2);
  (void)TextOut(hdc, column, PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)written);
  PANEL.top = (int16_t)(PANEL.top + LINE);
  (void)SelectObject(hdc, g_font_note_small_maybe);
  uint16_t note = (uint16_t)PENDING(3, string_load(0x2cb, DS_TEXT_BUFFER));
  (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)note, BP_FP(-0x34), DT_WORDBREAK_NOPREFIX);
}

/* Stargates and mass drivers: what the device does, its safe hull mass and range (stargates) or speed
 * (drivers), and the warning about the receiving planet or exceeding the limits. */
static void paint_stargate_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  /* the string of what the part does (BP-2), by item: stargates 0, 2, 3 are barred to Hyper-Expansion races, ... */
  const uint8_t item = request_item(req);
  int16_t blurb;
  if (item > 0xf) {
    blurb = -1;
  } else if (item == 0 || item == 2 || item == 3) {
    DESCRIPTION = 0x42;
    blurb = 0x2d0;
  } else if (item == 1 || item == 4 || item == 5 || item == 6) {
    DESCRIPTION = 0x2e;
    blurb = 0x2d0;
  } else if (item == 9 || item == 12) {
    blurb = 0x2d1;
  } else {
    DESCRIPTION = 0x2f;
    blurb = 0x2d1;
  }
  uint16_t length = string_load((uint16_t)blurb, DS_TEXT_BUFFER);
  BP_LOCAL(uint16_t, -0x14) = length;
  draw_text_flow(hdc, (uint16_t)(rt_bp - 0x2a), (uint16_t)(rt_bp - 0x26), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = (int16_t)(BP_LOCAL(int16_t, -0x26) + three_halves(LINE));
  if (blurb == 0x2d0) {
    /* a stargate: safe hull mass and safe range, then the warning about exceeding them */
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    uint16_t label = (uint16_t)PENDING(2, string_near(0x441));
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), label, 0, 1);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (record_word(req, 0x34) == 0xffff) {
      length = string_load(0x2dc, DS_TEXT_BUFFER);
    } else {
      length = format_number(g_fmt_ptr_word_maybe, record_word(req, 0x34));
    }
    BP_LOCAL(uint16_t, -0x14) = length;
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), DS_TEXT_BUFFER, length, 1);
    BP_LOCAL(int16_t, -0x2a) = PANEL.left;
    BP_LOCAL(int16_t, -0x26) = (int16_t)(BP_LOCAL(int16_t, -0x26) + LINE);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    label = (uint16_t)PENDING(2, string_near(0x442));
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), label, 0, 1);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (record_word(req, 0x36) == 0xffff) {
      length = string_load(0x2dc, DS_TEXT_BUFFER);
    } else {
      push_word(record_word(req, 0x36));
      uint16_t format = string_near(0x318);
      length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(1);
    }
    BP_LOCAL(uint16_t, -0x14) = length;
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), DS_TEXT_BUFFER, length, 1);
    if (record_word(req, 0x34) == 0xffff) {
      if (record_word(req, 0x36) == 0xffff) return;
      push_word((uint16_t)(record_word(req, 0x36) * 5));
      uint16_t format = string_near(0x444);
      length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(1);
    } else if (record_word(req, 0x36) == 0xffff) {
      push_word((uint16_t)(record_word(req, 0x34) * 5));
      uint16_t format = string_near(0x445);
      length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(1);
    } else {
      push_word((uint16_t)(record_word(req, 0x36) * 5));
      push_word((uint16_t)(record_word(req, 0x34) * 5));
      uint16_t format = string_near(0x443);
      length = (uint16_t)_wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(2);
    }
    BP_LOCAL(uint16_t, -0x14) = length;
  } else if (blurb == 0x2d1) {
    /* a mass driver: its warp speed and the warning about the receiving planet */
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    uint16_t label = (uint16_t)PENDING(2, string_near(0x344));
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), label, 0, 1);
    (void)SelectObject(hdc, g_font_arial_8pt);
    length = format_number(g_fmt_ptr_a_maybe, record_word(req, 0x34));
    draw_text_run(hdc, (uint16_t)(rt_bp - 0x2a), BP_LOCAL(int16_t, -0x26), DS_TEXT_BUFFER, length, 1);
    BP_LOCAL(int16_t, -0x2a) = PANEL.left;
    BP_LOCAL(int16_t, -0x26) = (int16_t)(BP_LOCAL(int16_t, -0x26) + LINE);
    blurb = 0x446;
    if (request_item(req) > 7) blurb = 0x447;
    length = string_load((uint16_t)blurb, DS_TEXT_BUFFER);
    BP_LOCAL(uint16_t, -0x14) = length;
  }
  if (blurb == -1) return;
  /* the warning, in the small note font and red */
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = (int16_t)(BP_LOCAL(int16_t, -0x26) + (LINE > 0xe ? 0 : 4) + LINE);
  (void)SelectObject(hdc, g_font_note_small_maybe);
  (void)SetTextColor(hdc, 0x7f);
  draw_text_flow(hdc, (uint16_t)(rt_bp - 0x2a), (uint16_t)(rt_bp - 0x26), DS_TEXT_BUFFER, BP_LOCAL(uint16_t, -0x14), PANEL.left,
                 (int16_t)(PANEL.right - PANEL.left), 0, 1);
  (void)SelectObject(hdc, g_font_arial_8pt);
  (void)SetTextColor(hdc, 0);
}

/* Starbase hulls: whether the hull has a space dock, then the dock capacity, armor and initiative. */
static void paint_starbase_hull_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  BP_LOCAL(uint16_t, -2) = 0x4a;
  BP_LOCAL(int16_t, -6) = PANEL.left;
  BP_LOCAL(int16_t, -4) = PANEL.top;
  const uint8_t item = request_item(req);
  if (item == 1 || item == 3) DESCRIPTION = 0x2cd;
  else if (item == 4) DESCRIPTION = 0x156;
  (void)SelectObject(hdc, g_font_arial_8pt);
  uint16_t length;
  if (record_word(req, 0x34) != 0) {
    length = string_load(0x2ce, DS_TEXT_BUFFER);
    draw_text_flow(hdc, (uint16_t)(rt_bp - 6), (uint16_t)(rt_bp - 4), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    BP_LOCAL(int16_t, -4) = (int16_t)(PANEL.top + LINE * 3);
    const int16_t width = (int16_t)(PANEL.right - PANEL.left);
    BP_LOCAL(int16_t, -6) = (int16_t)(((int16_t)(width - (width < 0 ? -1 : 0)) >> 1) + PANEL.left);
    length = string_load(0x4e2, DS_TEXT_BUFFER);
    draw_text_at(hdc, BP_LOCAL(int16_t, -6), BP_LOCAL(int16_t, -4), DS_TEXT_BUFFER, length, 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (record_word(req, 0x34) == 0xffff) length = string_load(0x2dc, DS_TEXT_BUFFER);
    else length = format_number(g_fmt_ptr_word_maybe, record_word(req, 0x34));
    (void)TextOut(hdc, BP_LOCAL(int16_t, -6), BP_LOCAL(int16_t, -4), ds_far(DS_TEXT_BUFFER), (int16_t)length);
  } else {
    length = string_load(0x2cf, DS_TEXT_BUFFER);
    draw_text_flow(hdc, (uint16_t)(rt_bp - 6), (uint16_t)(rt_bp - 4), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
    BP_LOCAL(int16_t, -4) = (int16_t)(PANEL.top + LINE * 3);
  }
  BP_LOCAL(int16_t, -4) = (int16_t)(BP_LOCAL(int16_t, -4) + three_halves(LINE));
  const int16_t width = (int16_t)(PANEL.right - PANEL.left);
  BP_LOCAL(int16_t, -6) = (int16_t)(((int16_t)(width - (width < 0 ? -1 : 0)) >> 1) + PANEL.left);
  uint16_t label = BP_LOCAL(uint16_t, -2);
  for (uint16_t row = 0; row < 2; row++) {
    length = string_load(label, DS_TEXT_BUFFER);
    BP_LOCAL(uint16_t, -0x14) = length;
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    draw_text_at(hdc, BP_LOCAL(int16_t, -6), BP_LOCAL(int16_t, -4), DS_TEXT_BUFFER, length, 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (row == 0) {
      length = format_number(g_fmt_ptr_a_maybe, record_word(req, 0x38));
      BP_LOCAL(uint16_t, -0x14) = length;
    } else {
      const uint16_t index[1] = {record_word(req, 0)};
      uint32_t hull = rt_call_far(MK_FP(SEG_SEL(2), 0x507c), FAR_CDECL, index, 1);
      length = format_number(g_fmt_ptr_a_maybe, (uint16_t)(*(uint8_t *)LIN(MK_FP(FP_SEG(hull), (uint16_t)(FP_OFF(hull) + 0x7b))) & 0x3f));
      BP_LOCAL(uint16_t, -0x14) = length;
    }
    (void)TextOut(hdc, BP_LOCAL(int16_t, -6), BP_LOCAL(int16_t, -4), ds_far(DS_TEXT_BUFFER), (int16_t)BP_LOCAL(uint16_t, -0x14));
    label++;
    BP_LOCAL(int16_t, -4) = (int16_t)(BP_LOCAL(int16_t, -4) + three_halves(LINE));
  }
}

/* The note with one formatted number several electrical and mechanical items use: the format is string
 * `format_id`, `value` the number, the text goes to the buffer and is flowed into the panel. */
static void paint_formatted_note(uint16_t hdc, uint16_t format_id, uint16_t value)
{
  push_word(value);
  uint16_t format = string_near(format_id);
  int16_t length = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
  pop_words(1);
  paint_flow_note(hdc, (uint16_t)length);
}
/* Shared tail of the electrical and mechanical kinds: a note string by id (NO_DESCRIPTION: none). */
static void paint_note_string(uint16_t hdc, uint16_t string_id)
{
  if (string_id == NO_DESCRIPTION) return;
  paint_flow_note(hdc, string_load(string_id, DS_TEXT_BUFFER));
}

/* Electrical parts (cloaks, jammers, computers, capacitors, dampeners): a note describing the part. */
static void paint_electrical_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  const uint8_t item = request_item(req);
  uint16_t note = NO_DESCRIPTION;         /* BP-2: the string of the note */
  uint16_t description = DESCRIPTION;     /* SI: the string of the line under the stats */
  switch (item > 0x10 ? 0x11 : item) {
    case 0: note = 0xd7; description = 0x39; break;
    case 1:
    case 2:
    case 3: {
      uint16_t percent;
      if (item == 3) { DESCRIPTION = 0x39; percent = 0x55; }
      else if (item == 1) percent = 0x23;
      else percent = 0x37;
      paint_formatted_note(hdc, 0xd8, percent);
      return;
    }
    case 4: note = 0xd9; description = 0x391; break;
    case 5:
    case 6:
    case 7: {
      push_word((uint16_t)(request_item(req) - 4));
      push_word(record_word(req, 0x34));
      uint16_t format = string_near(0x390);
      int16_t length = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
      pop_words(2);
      paint_flow_note(hdc, (uint16_t)length);
      return;
    }
    case 8:
    case 11: DESCRIPTION = 0x38e; /* fall through */
    case 9:
    case 10: paint_formatted_note(hdc, 0x38f, record_word(req, 0x34)); return;
    case 12:
    case 13:
      if (item == 13) DESCRIPTION = 0x389;
      paint_formatted_note(hdc, 0x38d, request_item(req) == 0xc ? 10 : 0x14);
      return;
    case 14: description = 0x387; note = 0x38a; break;
    case 15: description = 0x386; note = 0x38b; break;
    case 16: description = 0x388; note = 0x38c; break;
    default: break;
  }
  DESCRIPTION = description;
  paint_note_string(hdc, note);
}

/* Mechanical parts (modules, cargo and fuel pods, thrusters, the jump gate, the beam deflector). */
static void paint_mechanical_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -0x2a) = PANEL.left;
  BP_LOCAL(int16_t, -0x26) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  const uint8_t item = request_item(req);
  switch (item > 0xa ? 0xb : item) {
    case 0: DESCRIPTION = 0x159; paint_note_string(hdc, 0xd3); return;
    case 1: DESCRIPTION = 0x15a; paint_note_string(hdc, 0xd4); return;
    case 2:
    case 3: paint_formatted_note(hdc, 0xd1, request_item(req) == 2 ? 0x32 : 0x64); return;
    case 4: DESCRIPTION = 0x391; paint_note_string(hdc, 0xd2); return;
    case 5:
    case 6: paint_formatted_note(hdc, 0xd0, request_item(req) == 5 ? 0xfa : 0x1f4); return;
    case 7:
    case 8: paint_formatted_note(hdc, 0x186, request_item(req) == 7 ? 4 : 2); return;
    case 9: DESCRIPTION = 0x391; paint_note_string(hdc, 0xd5); return;
    case 10: paint_note_string(hdc, 0xd6); return;
    default: return;
  }
}

/* Terraformers: what environment variables they change and by how much. */
static void paint_terraformer_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -2) = PANEL.left;
  BP_LOCAL(int16_t, -4) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  int16_t length;
  if (request_item(req) <= 7) {
    DESCRIPTION = 0x158;
    (void)string_load(0x15e, (uint16_t)(rt_bp - 0x136));
    push_word(record_word(req, 0x34));
    length = _wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
    pop_words(1);
  } else {
    (void)string_load(0x15f, (uint16_t)(rt_bp - 0x136));
    push_word(record_word(req, 0x34));
    const int16_t which = (int16_t)request_item(req) - 8;
    const uint16_t name = NEAR_AT(uint16_t, (uint16_t)(0x48e + 2 * (uint16_t)((which + (which < 0 ? 3 : 0)) >> 2)));
    push_word(MEM_SEL_DGROUP);
    push_word(name);
    length = _wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
    pop_words(3);
  }
  BP_LOCAL(uint16_t, -0x14) = (uint16_t)length;
  if (length <= 0) return;
  draw_text_flow(hdc, (uint16_t)(rt_bp - 2), (uint16_t)(rt_bp - 4), DS_TEXT_BUFFER, (uint16_t)length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
}

/* Ship hulls: fuel capacity, cargo capacity, armor strength and initiative, then a note for some hulls. */
static void paint_ship_hull_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  const int16_t width = (int16_t)(PANEL.right - PANEL.left);
  BP_LOCAL(int16_t, -4) = (int16_t)(((int16_t)(width - (width < 0 ? -1 : 0)) >> 1) + PANEL.left);
  BP_LOCAL(uint16_t, -2) = 0x48;
  const uint8_t item = request_item(req);
  uint16_t description = DESCRIPTION;
  switch (item) {
    case 0xe:
    case 0x1f: description = 0x14f; break;
    case 3:
    case 0x19: description = 0x155; break;
    case 0x17: description = 0x150; break;
    case 0x14:
    case 0x16:
    case 0x18: description = 0x14b; break;
    case 0xa:
    case 8: description = 0x153; break;
    case 0xc:
    case 0x12: description = 0x154; break;
    case 0x1b:
    case 0x1c: description = 0x161; break;
    case 4:
    case 5:
    case 6: description = 0x30; break;
    case 0x1e: description = 0x392; break;
    default: break;
  }
  DESCRIPTION = description;
  for (uint16_t row = 0; row < 4; row++) {
    uint16_t length = string_load(BP_LOCAL(uint16_t, -2), DS_TEXT_BUFFER);
    BP_LOCAL(uint16_t, -0x14) = length;
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    draw_text_at(hdc, BP_LOCAL(int16_t, -4), PANEL.top, DS_TEXT_BUFFER, length, 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (row == 0) {
      length = format_number(DS_STR_MG_FORMAT, record_word(req, 0x36));
    } else if (row == 1) {
      length = format_number(g_fmt_ptr_word_maybe, record_word(req, 0x34));
    } else if (row == 2) {
      length = format_number(g_fmt_ptr_a_maybe, record_word(req, 0x38));
    } else {
      const uint16_t index[1] = {record_word(req, 0)};
      uint32_t hull = rt_call_far(MK_FP(SEG_SEL(2), 0x507c), FAR_CDECL, index, 1);
      length = format_number(g_fmt_ptr_a_maybe, (uint16_t)(*(uint8_t *)LIN(MK_FP(FP_SEG(hull), (uint16_t)(FP_OFF(hull) + 0x7b))) & 0x3f));
    }
    BP_LOCAL(uint16_t, -0x14) = length;
    (void)TextOut(hdc, BP_LOCAL(int16_t, -4), PANEL.top, ds_far(DS_TEXT_BUFFER), (int16_t)length);
    BP_LOCAL(uint16_t, -2) = (uint16_t)(BP_LOCAL(uint16_t, -2) + 1);
    PANEL.top = (int16_t)(PANEL.top + three_halves(LINE));
  }
  const uint8_t item2 = request_item(req);
  if (item2 == 0x19 || item2 == 0x1a) {
    /* a hull that makes fuel (and heals the fleet) */
    uint16_t format = string_near(0x4a6);
    BP_LOCAL(uint16_t, -2) = (request_item(req) == 0x19) ? 5 : 10;
    push_word(BP_LOCAL(uint16_t, -2));
    int16_t length = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
    pop_words(1);
    BP_LOCAL(uint16_t, -0x14) = (uint16_t)length;
    (void)SelectObject(hdc, g_font_arial_8pt);
    BP_LOCAL(int16_t, -0x26) = PANEL.top;
    BP_LOCAL(int16_t, -4) = PANEL.left;
    draw_text_flow(hdc, (uint16_t)(rt_bp - 4), (uint16_t)(rt_bp - 0x26), DS_TEXT_BUFFER, (uint16_t)length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
  } else if (item2 == 0x1b || item2 == 0x1c) {
    /* a hull that doubles the efficiency of the mine laying pods */
    uint16_t length = string_load(0x4a7, DS_TEXT_BUFFER);
    (void)SelectObject(hdc, g_font_arial_8pt);
    BP_LOCAL(int16_t, -0x26) = PANEL.top;
    BP_LOCAL(int16_t, -4) = PANEL.left;
    draw_text_flow(hdc, (uint16_t)(rt_bp - 4), (uint16_t)(rt_bp - 0x26), DS_TEXT_BUFFER, length, PANEL.left, (int16_t)(PANEL.right - PANEL.left), 0, 1);
  }
}

/* Planetary scanners and defences: scanners show their range (a negative range is a stealth scanner),
 * defences a chart of the damage they do against the quantity (standard and smart), the Genesis Device a note. */
static void paint_planetary_item(uint16_t hdc, uint16_t req)
{
  shrink_rect((uint16_t)(rt_bp - 0x34), -4, -4);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  BP_LOCAL(int16_t, -0x1a) = PANEL.left;
  BP_LOCAL(int16_t, -0x1e) = PANEL.top;
  (void)SelectObject(hdc, g_font_arial_8pt);
  BP_LOCAL(uint16_t, -0x14) = 0;
  const uint8_t item = request_item(req);
  if (item >= 9 && item <= 0xd) {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    PANEL.right = (int16_t)(PANEL.right - 2);
    uint16_t length = string_load(0x28, DS_TEXT_BUFFER);
    BP_LOCAL(uint16_t, -0x14) = length;
    draw_text_centered(hdc, (int16_t)(((int16_t)(PANEL.right - PANEL.left) >> 1) + PANEL.left), PANEL.top, DS_TEXT_BUFFER, length);
    PANEL.top = (int16_t)(PANEL.top + LINE);
    const int16_t number_width = text_width(hdc, DS_STR_NUM_SAMPLE, 3);
    const int16_t column_width = (int16_t)((int16_t)(PANEL.right - PANEL.left - number_width - 10) / 5);
    const int16_t plot_height = (int16_t)(PANEL.bottom - PANEL.top - LINE);
    const int16_t row_height = (int16_t)((int16_t)(plot_height - 8) / 5);
    (void)PatBlt(hdc, (int16_t)(PANEL.left + number_width + 6), PANEL.top, 1, (int16_t)(plot_height - 4), BLACKNESS);
    (void)PatBlt(hdc, (int16_t)(PANEL.left + number_width + 6), (int16_t)(PANEL.bottom - LINE - 4),
                 (int16_t)(PANEL.right - PANEL.left - number_width - 6), 1, BLACKNESS);
    int16_t origin_x = (int16_t)(PANEL.left + number_width);
    int16_t line_y = (int16_t)(PANEL.bottom - 5 * row_height - LINE - 4);
    (void)SetTextColor(hdc, 0x007f0000);
    draw_text_at(hdc, PANEL.right, (int16_t)(PANEL.bottom + 3 * (int16_t)(0xffff - LINE)), DS_STR_STANDARD, 8, 0);
    (void)SetTextColor(hdc, 0x7f);
    draw_text_at(hdc, PANEL.right, (int16_t)(PANEL.bottom + 2 * (int16_t)(0xfffe - LINE)), DS_STR_SMART, 5, 0);
    (void)SetTextColor(hdc, 0);
    /* the percent scale: 100, 80, 60, 40, 20 */
    int16_t y = line_y;
    for (int16_t percent = 100; percent != 0; percent = (int16_t)(percent - 0x14)) {
      length = format_number(g_fmt_percent, (uint16_t)percent);
      draw_text_at(hdc, origin_x, (int16_t)(y - (LINE >> 1)), DS_TEXT_BUFFER, length, 0);
      (void)PatBlt(hdc, (int16_t)(origin_x + 4), y, 5, 1, BLACKNESS);
      y = (int16_t)(y + row_height);
    }
    int16_t axis_label_y = (int16_t)(PANEL.bottom - LINE);
    draw_text_at(hdc, origin_x, axis_label_y, DS_STR_NUM, 3, 0);
    origin_x = (int16_t)(origin_x + 6);
    const int16_t zero_width = text_width(hdc, DS_STR_0_AXIS, 1);
    /* the quantity axis: 0, 20, ..., 100 */
    for (int16_t quantity = 0; quantity <= 100; quantity = (int16_t)(quantity + 0x14)) {
      length = format_number(g_fmt_ptr_a_maybe, (uint16_t)quantity);
      int16_t shift;
      if (quantity == 0) shift = (int16_t)(zero_width >> 1);
      else if (quantity == 100) shift = (int16_t)((int16_t)(zero_width * 3 - (zero_width * 3 < 0 ? -1 : 0)) >> 1);
      else shift = zero_width;
      (void)TextOut(hdc, (int16_t)(origin_x - shift), axis_label_y, ds_far(DS_TEXT_BUFFER), (int8_t)length);
      if (quantity > 0) (void)PatBlt(hdc, origin_x, (int16_t)(axis_label_y - 6), 1, 5, BLACKNESS);
      origin_x = (int16_t)(origin_x + column_width);
    }
    /* the two damage curves */
    const uint16_t old_pen = SelectObject(hdc, g_pen_chart_maybe);
    const int16_t curve_x0 = (int16_t)(number_width + PANEL.left + 6);
    const int16_t curve_y0 = (int16_t)(PANEL.bottom - LINE - 4);
    int32_t falloff = (int32_t)(int16_t)(0x3e8 - (int16_t)record_word(req, 0x34));
    for (int16_t pass = 2; pass != 0; pass--) {
      int32_t damage = 1000000;
      for (int16_t quantity = 0; quantity <= 100; quantity++) {
        const int16_t x = (int16_t)(MulDiv(quantity, column_width, 0x14) + curve_x0);
        const int32_t scaled = (int32_t)((uint32_t)(damage - 1000000) * (uint32_t)(int32_t)row_height);
        const int16_t y_curve = (int16_t)((int16_t)(scaled / 200000) + curve_y0);
        damage = (int32_t)((uint32_t)falloff * (uint32_t)damage) / 1000;
        if (quantity == 0) (void)MoveTo(hdc, x, y_curve);
        else (void)LineTo(hdc, x, y_curve);
      }
      const int16_t half = (int16_t)record_word(req, 0x34);
      falloff = (int32_t)(int16_t)(0x3e8 - (int16_t)((int16_t)(half - (half < 0 ? -1 : 0)) >> 1));
      (void)SelectObject(hdc, g_pen_default_maybe);
    }
    (void)SelectObject(hdc, old_pen);
    BP_LOCAL(uint16_t, -0x14) = 0;
    DESCRIPTION = (request_item(req) > 0xa) ? 0x157 : 0x4e0;
  } else if (item <= 8) {
    const int16_t range = (int16_t)record_word(req, 0x34);
    (void)string_load(0x31, (uint16_t)(rt_bp - 0x136));
    push_word((uint16_t)(range < 0 ? -range : range));
    int16_t length = _wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
    pop_words(1);
    if (range >= 0) {
      BP_LOCAL(uint16_t, -0x14) = (uint16_t)length;
      DESCRIPTION = 0x4e0;
    } else {
      draw_text_flow(hdc, (uint16_t)(rt_bp - 0x1a), (uint16_t)(rt_bp - 0x1e), DS_TEXT_BUFFER, (uint16_t)length, PANEL.left,
                     (int16_t)(PANEL.right - PANEL.left), 0, 1);
      BP_LOCAL(int16_t, -0x1a) = PANEL.left;
      BP_LOCAL(int16_t, -0x1e) = (int16_t)(BP_LOCAL(int16_t, -0x1e) + three_halves(LINE));
      (void)string_load(0x34, (uint16_t)(rt_bp - 0x136));
      push_word((uint16_t)((int16_t)(-range) >> 1));
      length = _wsprintf(ds_far(DS_TEXT_BUFFER), BP_FP(-0x136), MK_FP(rt_ss, rt_sp));
      pop_words(1);
      DESCRIPTION = 0x160;
      BP_LOCAL(uint16_t, -0x14) = (uint16_t)length;
    }
  } else if (item == 0xe) {
    DESCRIPTION = 0x393;
    BP_LOCAL(uint16_t, -0x14) = string_load(0x398, DS_TEXT_BUFFER);
  }
  if ((int16_t)BP_LOCAL(uint16_t, -0x14) > 0) {
    draw_text_flow(hdc, (uint16_t)(rt_bp - 0x1a), (uint16_t)(rt_bp - 0x1e), DS_TEXT_BUFFER, BP_LOCAL(uint16_t, -0x14), PANEL.left,
                   (int16_t)(PANEL.right - PANEL.left), 0, 1);
  }
}

/* paint_tech_item_panel: 27:0x1e06 (Ghidra FUN_10d0_1e06). 11048 bytes (to 0x492d); the 7983 bytes
 * after 0x29ff (the code of the kinds) are listed in docs/code-segment-data.md. ENTER 0x138,0; PUSH DI; PUSH SI. FAR CDECL (hdc [BP+6], width [BP+8],
 * height [BP+0xA], request [BP+0xC] a near pointer to the tech item request); no result.
 * Draws the item panel of the tech browser into a window of width by height pixels: a bevelled frame, the
 * name of the item in the 10 point bold font, its picture (a 64 by 64 bitmap from the sheet words
 * DS:0x2808, or the hull drawing 10:0x2d28 for hulls), the tech levels it requires ("Tech Req:", red when the
 * player lacks the level), the research cost (27:0x49ee, "Cost: ..k", "UnAvail" or "Available"), the
 * cost rows (10:0x7a92: iron, boranium, germanium and resources of the item for the current player), the
 * mass, and then the stats that depend on the kind (one function per kind above, chosen by the
 * request's kind word); the note under the stats is the string DESCRIPTION, drawn at the bottom in red
 * when the item cannot be researched. Without a record (request +4 is 0) only "None Available" is drawn.
 * Frame: the text box RECT at BP-0x34, the id of the note at BP-0x20, the research cost dword at BP-0x10,
 * the cost buffer of 10:0x7a92 at BP-0x3c, a string buffer at BP-0x136 and BP-0x104. */
DECOMP(27, 0x1e06, paint_tech_item_panel, FAR_CDECL)
void paint_tech_item_panel(uint16_t hdc, int16_t width, int16_t height, uint16_t req)
{
  FRAME_ENTER_REGS(0x138, 2);
  (void)SelectObject(hdc, g_brush_btnshadow);
  (void)PatBlt(hdc, 0, (int16_t)(height - 1), width, 1, PATCOPY);
  (void)PatBlt(hdc, (int16_t)(width - 1), 0, 1, height, PATCOPY);
  (void)SelectObject(hdc, g_brush_btnhighlight);
  (void)PatBlt(hdc, 0, 0, width, 1, PATCOPY);
  (void)PatBlt(hdc, 0, 0, 1, height, PATCOPY);
  (void)PatBlt(hdc, 4, (int16_t)((int16_t)(0xffff - LINE) * 2 + height), (int16_t)(width - 8), 1, PATCOPY);
  (void)SetBkMode(hdc, 1);
  if (request_record(req) == 0) {
    /* no item: "None Available" in the middle */
    (void)SetRect(BP_FP(-0x34), 5, 5, (int16_t)(width - 5), (int16_t)(height - 5));
    uint16_t text = (uint16_t)PENDING(1, string_near(0x33c));
    (void)CALL_ORIG(8, 0x9482, FAR_CDECL, 4, hdc, (uint16_t)(rt_bp - 0x34), text, 0x000e);
    FRAME_LEAVE();
    return;
  }
  (void)SelectObject(hdc, g_font_arial_bold_10pt);
  {
    const uint8_t *name = (const uint8_t *)LIN(record_far(req, 8));
    memcpy(LINN(DS_TEXT_BUFFER), name, (size_t)cstr_length(name) + 1);
  }
  draw_text_centered(hdc, (int16_t)(width >> 1), 3, DS_TEXT_BUFFER, 0);
  const uint16_t kind = request_kind(req);
  if (kind != TECH_KIND_SHIP_HULL && kind != TECH_KIND_STARBASE_HULL) {
    /* a 64 by 64 picture cut from a bitmap sheet */
    (void)SelectPalette(hdc, g_palette, 0);
    (void)RealizePalette(hdc);
    const uint16_t picture = record_word(req, 0x32);
    const uint16_t source_x = (uint16_t)((picture & 7) << 6);
    const uint8_t shifted = (uint8_t)((uint8_t)(picture & 0xf8) << 3);
    const uint16_t source_y = (uint16_t)((uint8_t)(0xc7 - shifted) & 0xc0);
    const uint16_t sheet = NEAR_AT(uint16_ua, (uint16_t)(0x2808 + ((int16_t)(picture & 0xffef) >> 4)));
    (void)CALL_ORIG(8, 0xaa4e, FAR_CDECL, 12, hdc, 5, (uint16_t)(g_cell_height_10pt + 5), 0x40, 0x40, sheet, source_x, source_y, 0x40, 0x40,
                    (uint16_t)SRCCOPY, (uint16_t)(SRCCOPY >> 16));
  } else {
    (void)CALL_ORIG(10, 0x2d28, FAR_CDECL, 11, 0, 0, hdc, 5, (uint16_t)(g_cell_height_10pt + 5), 0, record_word(req, 0x32), 0, 0, 0xffff, 0);
  }
  /* "Tech Req:" and the levels the item requires */
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  int16_t line_y = (int16_t)(g_cell_height_10pt + 0x47);
  uint16_t tech_req_text = (uint16_t)PENDING(3, string_near(0x33d));
  (void)TextOut(hdc, 5, line_y, ds_far(tech_req_text), 9);
  int16_t label_width = 0;
  for (int16_t field = 0; field < TECH_FIELD_COUNT; field++) {
    uint16_t length = (uint16_t)PENDING(3, string_load((uint16_t)(0x5b + field), DS_TEXT_BUFFER));
    int16_t w = text_width(hdc, DS_TEXT_BUFFER, (int16_t)length);
    if (w > label_width) label_width = w;
  }
  const int16_t level_sample = text_width(hdc, DS_STR_99, 2);
  label_width = (int16_t)(label_width + 5);
  const int16_t level_x = (int16_t)(level_sample + label_width);
  BP_LOCAL(int16_t, -8) = level_x; /* the original keeps the column in this slot; the kind code below may read what is left in it */
  bool any_requirement = false;
  for (int16_t field = 0; field < TECH_FIELD_COUNT; field++) {
    const int8_t needed = *(const int8_t *)LIN(record_far(req, (uint16_t)(2 + field)));
    if (needed <= 0) continue;
    line_y = (int16_t)(line_y + LINE);
    (void)SetTextColor(hdc, (needed <= *player_tech_level(g_current_player, (uint16_t)field)) ? 0 : 0x7f);
    uint16_t length = (uint16_t)PENDING(1, string_load((uint16_t)(0x5b + field), DS_TEXT_BUFFER));
    draw_text_at(hdc, label_width, line_y, DS_TEXT_BUFFER, length, 0);
    (void)SetTextColor(hdc, 0);
    length = format_number(g_fmt_ptr_a_maybe, (uint16_t)(int16_t)*(const int8_t *)LIN(record_far(req, (uint16_t)(2 + field))));
    draw_text_at(hdc, level_x, line_y, DS_TEXT_BUFFER, length, 0);
    any_requirement = true;
  }
  if (!any_requirement) {
    line_y = (int16_t)(line_y + LINE);
    uint16_t none_text = (uint16_t)PENDING(1, string_near(0x33e));
    draw_text_centered(hdc, 0x25, line_y, none_text, 0x000c);
  }
  /* the research cost of the item */
  uint32_t cost;
  {
    const uint16_t ask[1] = {req};
    if ((int16_t)rt_call_far(MK_FP(SEG_SEL(2), 0x50f8), FAR_CDECL, ask, 1) <= 0) {
      cost = 0xffffffffu;
    } else {
      const uint32_t record = request_record(req);
      const uint16_t args[2] = {(uint16_t)(FP_OFF(record) + 2), FP_SEG(record)};
      cost = rt_call_far(MK_FP(SEG_SEL(27), 0x49ee), FAR_CDECL, args, 2);
    }
    BP_LOCAL(uint32_t, -0x10) = cost;
  }
  if ((int32_t)cost > 99999 || (int32_t)cost > 0) {
    const uint32_t shown = ((int32_t)cost > 99999) ? (uint32_t)(((int32_t)cost + 500) / 1000) : cost;
    push_dword(shown);
    uint16_t format = string_near((int32_t)cost > 99999 ? 0x33f : 0x340);
    int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
    pop_words(2);
    (void)TextOut(hdc, 5, (int16_t)(LINE + line_y + 4), ds_far(DS_TEXT_BUFFER), written);
  } else if (cost == 0xffffffffu) {
    (void)SetTextColor(hdc, 0x7f);
    uint16_t unavailable = (uint16_t)PENDING(1, string_near(0x341));
    draw_text_centered(hdc, 0x25, (int16_t)(LINE + line_y + 4), unavailable, 7);
    (void)SetTextColor(hdc, 0);
  } else {
    uint16_t available = (uint16_t)PENDING(1, string_near(0x342));
    line_y = (int16_t)(line_y + LINE + 4);
    draw_text_centered(hdc, 0x25, line_y, available, 9);
  }
  /* the cost rows: ironium, boranium, germanium and resources */
  int16_t row_y = (int16_t)(g_cell_height_10pt + 5);
  if (LINE * 4 < 0x40) row_y = (int16_t)(row_y + (int16_t)((int16_t)((0x10 - LINE) * 4) >> 1));
  const int16_t cost_top = row_y;
  BP_LOCAL(int16_t, -0xa) = cost_top;
  BP_LOCAL(int16_t, -2) = DS_COST_COLORS; /* the row pointer's start: left in the frame, like the slots above and below */
  int16_t cost_label_width = 0;                                     /* BP-0x1c */
  for (uint16_t at = DS_COST_LABELS; at <= DS_COST_LABELS + 10; at = (uint16_t)(at + 2)) {
    const uint16_t label = NEAR_AT(uint16_t, at);
    int16_t w = (int16_t)(uint16_t)GetTextExtent(hdc, ds_far(label), (int16_t)lstrlen(ds_far(label)));
    if (w > cost_label_width) cost_label_width = w;
  }
  cost_label_width = (int16_t)(cost_label_width + 0x4d);
  const uint16_t cost_args[3] = {g_current_player, req, (uint16_t)(rt_bp - 0x3c)};
  (void)CALL_ORIG(10, 0x7a92, FAR_CDECL, 3, cost_args[0], cost_args[1], cost_args[2]);
  for (uint16_t row = 0; row < 6; row++) {
    const uint16_t color_at = (uint16_t)(DS_COST_COLORS + 4 * row);
    if (color_at == 0x0428 || color_at == 0x0424) continue; /* colonists and fuel have no row */
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)SetTextColor(hdc, NEAR_AT(uint32_ua, color_at));
    draw_text_at(hdc, cost_label_width, row_y, NEAR_AT(uint16_t, (uint16_t)(DS_COST_LABELS + 2 * row)), 0, 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)SetTextColor(hdc, g_color_windowtext);
    int16_t amount = (color_at != 0x042c) ? BP_LOCAL(int16_t, (int16_t)(-0x3c + 2 * row)) : BP_LOCAL(int16_t, -0x36);
    const uint16_t kind_now = request_kind(req);
    if (kind_now == TECH_KIND_STARBASE_HULL || kind_now == TECH_KIND_STARGATE_DRIVER) amount = (int16_t)(amount - (int16_t)(amount / 2));
    uint16_t length = format_number(g_fmt_ptr_a_maybe, (uint16_t)amount);
    draw_text_at(hdc, (int16_t)(cost_label_width + g_text_width_8_chars), row_y, DS_TEXT_BUFFER, length, 0);
    if (color_at < 0x042c) (void)TextOut(hdc, (int16_t)(cost_label_width + g_text_width_8_chars), row_y, ds_far(DS_STR_KT), 2);
    row_y = (int16_t)(row_y + LINE);
  }
  /* (the rows of the cost table above are in the order of DS:0x418; DS:0x424 and DS:0x428 are skipped) */
  /* the original counts the row y in BP-6 and the row index in BP-4; the mine layer reads both for an item past its table */
  BP_LOCAL(int16_t, -6) = row_y;
  BP_LOCAL(int16_t, -4) = 12;
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  if (record_word(req, 0x28) != 0) {
    push_word(record_word(req, 0x28));
    uint16_t format = string_near(0x343);
    int16_t written = _wsprintf(ds_far(DS_TEXT_BUFFER), ds_far(format), MK_FP(rt_ss, rt_sp));
    pop_words(1);
    (void)TextOut(hdc, (int16_t)(cost_label_width + g_text_width_8_chars + 0x20), cost_top, ds_far(DS_TEXT_BUFFER), written);
  }
  /* the box for the stats of the kind */
  (void)SetRect(BP_FP(-0x34), (int16_t)((int16_t)(level_x + 4) < 0x49 ? 0x49 : (int16_t)(level_x + 4)), (int16_t)(g_cell_height_10pt + 0x47),
                (int16_t)(width - 5), (int16_t)(-(int16_t)(LINE * 2 - height) - 9));
  if (LINE > 0xe) PANEL.left = (int16_t)(PANEL.left + 4);
  (void)SelectObject(hdc, g_brush_btnshadow);
  (void)PatBlt(hdc, PANEL.left, PANEL.top, (int16_t)(PANEL.right - PANEL.left), 1, PATCOPY);
  (void)PatBlt(hdc, PANEL.left, PANEL.top, 1, (int16_t)(PANEL.bottom - PANEL.top), PATCOPY);
  (void)SelectObject(hdc, g_brush_btnhighlight);
  (void)PatBlt(hdc, PANEL.left, (int16_t)(PANEL.bottom - 1), (int16_t)(PANEL.right - PANEL.left), 1, PATCOPY);
  (void)PatBlt(hdc, (int16_t)(PANEL.right - 1), PANEL.top, 1, (int16_t)(PANEL.bottom - PANEL.top), PATCOPY);
  shrink_rect((uint16_t)(rt_bp - 0x34), -3, -3);
  if ((g_ui_flags_d_maybe & 0x40) != 0 && cost == 0) {
    (void)SetTextColor(hdc, g_color_btnhighlight);
    (void)CALL_ORIG(8, 0x9482, FAR_CDECL, 4, hdc, (uint16_t)(rt_bp - 0x34), DS_STR_BLEEDING_EDGE, 0xd);
    (void)SetTextColor(hdc, g_color_windowtext);
  }
  DESCRIPTION = NO_DESCRIPTION;
  switch (request_kind(req)) {
    case TECH_KIND_ENGINE: paint_engine_item(hdc, req); break;
    case TECH_KIND_SCANNER: paint_scanner_item(hdc, req); break;
    case TECH_KIND_SHIELD: paint_shield_armor_item(hdc, req, 0x345, true); break;
    case TECH_KIND_ARMOR: paint_shield_armor_item(hdc, req, 0x346, false); break;
    case TECH_KIND_BEAM_WEAPON: paint_beam_item(hdc, req); break;
    case TECH_KIND_TORPEDO: paint_torpedo_item(hdc, req); break;
    case TECH_KIND_BOMB: paint_bomb_item(hdc, req); break;
    case TECH_KIND_MINING_ROBOT: paint_mining_robot_item(hdc, req); break;
    case TECH_KIND_MINE_LAYER: paint_mine_layer_item(hdc, req); break;
    case TECH_KIND_STARGATE_DRIVER: paint_stargate_item(hdc, req); break;
    case TECH_KIND_STARBASE_HULL: paint_starbase_hull_item(hdc, req); break;
    case TECH_KIND_ELECTRICAL: paint_electrical_item(hdc, req); break;
    case TECH_KIND_MECHANICAL: paint_mechanical_item(hdc, req); break;
    case TECH_KIND_TERRAFORMER: paint_terraformer_item(hdc, req); break;
    case TECH_KIND_SHIP_HULL: paint_ship_hull_item(hdc, req); break;
    case TECH_KIND_PLANETARY: paint_planetary_item(hdc, req); break;
    default: break;
  }
  /* the note under the stats */
  if (DESCRIPTION != NO_DESCRIPTION) {
    uint16_t length = string_load(DESCRIPTION, DS_TEXT_BUFFER);
    (void)SetRect(BP_FP(-8), 4, (int16_t)(-(int16_t)(LINE * 2 - height)), (int16_t)(width - 4), (int16_t)(height - 5));
    (void)SelectObject(hdc, LINE > 0xe ? g_font_note_large_maybe : g_font_note_small_maybe);
    (void)SetTextColor(hdc, (int32_t)BP_LOCAL(uint32_t, -0x10) == -1 ? 0x7f : 0);
    (void)DrawText(hdc, ds_far(DS_TEXT_BUFFER), (int16_t)length, BP_FP(-8), DT_WORDBREAK_NOPREFIX);
    if ((int32_t)BP_LOCAL(uint32_t, -0x10) == -1) (void)SetTextColor(hdc, 0);
  }
  FRAME_LEAVE();
}
#undef LINE
#undef DESCRIPTION
#undef PANEL
