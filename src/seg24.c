/* Decompiled code segment 24 (NE segment 24, selector 0x00C0).
 * Original: stars26i.exe. Oracle: the original machine code, diffed by
 * tests/scenarios/test_seg24.c; Ghidra's pseudo-C in re/decomp/seg24 is only a reading aid.
 *
 * Segment 24 is the information pop-up of the main window: when the player presses the mouse button
 * on a thing on the screen, the click handler (5:0x2ab4) fills the descriptor DS:0x0BC0 (what the
 * thing is and its numbers) and 24:0x091e measures the pop-up text, opens a small captured window of
 * class "starspopup" next to the mouse and the window procedure POPUPWNDPROC paints the text with
 * 24:0x00ca. Releasing a mouse button closes the window again. The segment also holds the pop-up menu
 * the program uses for its choice lists:
 *   24:0x0000  POPUPWNDPROC: window procedure of the pop-up (paint, erase background, close on release)
 *   24:0x00ca  paint_info_popup: paints the pop-up for the descriptor's kind (1 to 14)
 *   24:0x091e  show_info_popup: measures the text of the descriptor and opens the pop-up window
 *   24:0x0e24  popup_menu_choose: builds a pop-up menu from a list, tracks it, returns the choice
 *   24:0x116a  layout_environment_info: a planet environment value (gravity, temperature, radiation)
 *   24:0x18a4  layout_colonist_info: what a planet holds / would hold in colonists
 *   24:0x208c  layout_zip_order_help: the help text of the ZipOrd quick transport orders
 *   24:0x23ba  layout_mine_factory_info: the mines or factories of a planet
 *   24:0x25a2  layout_resource_info: the resources of a planet
 *   24:0x2738  layout_text_tooltip: one wrapped text (a tooltip) from the descriptor
 * Every layout_ function lays the text out and, when its last argument is non-zero, also draws it; it
 * returns the width of the text block plus 4 in AX and its height plus 4 in DX, which 24:0x091e uses
 * as the size of the window. The text helpers of segment 8 do the drawing: 8:0x90b0 (one piece of text
 * at the cursor, moves the cursor), 8:0x90fe (wrapped text), 8:0x9072 (centred text), 8:0x9398 (text
 * right-aligned at a position) and 8:0x8df4 / 8:0x8dc0 (a string of the string table); they are not
 * decompiled yet and stay CALL_ORIG.
 *
 * Names: POPUPWNDPROC is the export name. The other names describe what the function does (CONVENTIONS
 * rule f); names ending in _maybe are inferred. The old Ghidra names FUN_10b8_<off> are kept in the
 * comment above each function. Strings of the string table are named by their text (STR_*), the
 * literal strings in DGROUP by DS_STR_* / DS_FMT_*; the texts were read from the original by calling
 * 8:0x8df4 and from the data segment.
 *
 * The functions mirror their original frames (CONVENTIONS rule d): FRAME_ENTER_REGS(n, pushes) with n
 * the ENTER size and one push for each PUSH SI / PUSH DI. The stack slots whose addresses the
 * original hands to a callee (the cursor x and y, the rectangle, the text buffers) are BP_LOCAL slots
 * at the original offsets; the other locals are C variables. The compiler of the original pushes
 * the trailing arguments of a call (the length 0 and the draw flag of the text helpers) before an
 * earlier call and lets them stay on the stack; they are passed here as the arguments they are. The
 * functions of this segment that mirror a frame are reached through rt_call_far. */
#include <string.h>

#include "src/stars.h"

/* ---- the descriptor DS:0x0BC0 (written by 5:0x2ab4) ------------------------------------------------ */

/* Fields of the descriptor, as offsets from DS:0x0BC0 (22 bytes; the strings of DGROUP follow at
 * DS:0x0BD6). Each kind uses the fields differently, see the cases of paint_info_popup. */
enum {
  DS_INFO = 0x0bc0,
  INFO_KIND = 0x00,     /* word: what the pop-up shows, INFO_KIND_* (1 to 14) */
  INFO_ID = 0x02,       /* word: id of the thing; for the kinds 3, 11 and 14 the offset of a far pointer to its record */
  INFO_ARG_4 = 0x04,    /* word: second argument, for the kinds 5 and 7 the index of the environment value / planet field */
  INFO_ARG_6 = 0x06,    /* word or dword: third argument (low word of a dword in the kinds 1 and 5) */
  INFO_ARG_8 = 0x08,    /* word: high word of INFO_ARG_6, or a column width that show_info_popup stores for kind 3 */
  INFO_ARG_A = 0x0a,    /* word or dword */
  INFO_ARG_C = 0x0c,    /* word: high word of INFO_ARG_A */
  INFO_ARG_E = 0x0e,    /* word or dword */
  INFO_ARG_10 = 0x10,   /* word: high word of INFO_ARG_E */
  INFO_ARG_12 = 0x12,   /* dword (kind 1: the mining rate per year); INFO_ARG_14 is its high word */
  INFO_ARG_14 = 0x14
};
#define INFO_WORD(field) NEAR_AT(uint16_ua, DS_INFO + (field))
#define INFO_DWORD(field) NEAR_AT(uint32_ua, DS_INFO + (field))

/* The kinds of pop-up (the word at INFO_KIND): the jump tables of 24:0x00ca and 24:0x091e. */
enum {
  INFO_KIND_PLANET_MINERAL = 1,  /* one mineral of a planet: concentration, surface amount, mining rate */
  INFO_KIND_PLAYER = 2,          /* a player: the name and "Player #n" */
  INFO_KIND_FLEET = 3,           /* a fleet: its ships, with the damage of each design */
  INFO_KIND_PLACE = 4,           /* the selected place: planet name, id and position */
  INFO_KIND_ENVIRONMENT = 5,     /* one environment value of a planet and what terraforming does to it */
  INFO_KIND_ZIP_ORDER_HELP = 6,  /* the help text of the ZipOrd transport order sets */
  INFO_KIND_COLONISTS = 7,       /* the colonists of a planet now and in the future */
  INFO_KIND_MINE_FACTORY = 8,    /* the mines or factories of a planet */
  INFO_KIND_TECH_ITEM = 9,       /* one tech item, painted by 27:0x1e06 */
  INFO_KIND_TEXT = 10,           /* a text, the near pointer is in INFO_ARG_4 */
  INFO_KIND_DESIGN = 11,         /* a ship design picture and details */
  INFO_KIND_RESOURCES = 12,      /* the resources of a planet */
  INFO_KIND_EMPTY = 13,          /* an empty box */
  INFO_KIND_DESIGN_B = 14        /* like INFO_KIND_DESIGN, from another source (27:0x0000 passes a copy) */
};

/* ---- strings of the string table (8:0x8df4 / 8:0x8dc0), named by their text -------------------------- */

enum {
  STR_IF_YOU_TERRAFORM = 0x162,          /* "If you were to terraform " */
  STR_VALUE_WOULD_IMPROVE = 0x163,       /* ", the planet's value would improve to " */
  STR_FMT_OUTSIDE_RANGE = 0x164,         /* "This value is %d%% outside the habitable range for your race." */
  STR_FMT_AWAY_FROM_IDEAL = 0x165,       /* "This value is %d%% away from the ideal value for your race." */
  STR_NO_DEFENSES = 0x1df,               /* " appears to have no planetary defenses." */
  STR_FMT_DEFENSES = 0x1e0,              /* " has planetary defenses with approximately %d%% coverage." */
  STR_YOUR_POPULATION_ON_LONG = 0x20c,   /* "Your population on " */
  STR_FMT_WILL_GROW = 0x20d,             /* " will grow by %ld00 to %ld00 next year." */
  STR_WILL_NOT_GROW = 0x20e,             /* " will not grow next year." */
  STR_RESOURCE_INFO = 0x22d,             /* "Resource Info" */
  STR_CLEAR = 0x22e,                     /* "Clear:  " */
  STR_CLEAR_HELP = 0x22f,                /* "Remove all transport orders." */
  STR_WAIT_LOAD = 0x230,                 /* "WaitLoad:  " */
  STR_WAIT_LOAD_HELP = 0x231,            /* "Wait for a full load of minerals." */
  STR_QUICK_LOAD = 0x232,                /* "QuikLoad:  " */
  STR_QUICK_LOAD_HELP = 0x233,           /* "Load all minerals available." */
  STR_QUICK_DROP = 0x234,                /* "QuikDrop:  " */
  STR_QUICK_DROP_HELP = 0x235,           /* "Unload everything the fleet is carrying." */
  STR_ZIP_HELP_LINE_4 = 0x236,           /* "button, then click on the order of your choice." */
  STR_ZIP_HELP_LINE_3 = 0x237,           /* "a ZipOrd, click on this diamond with the right mouse" */
  STR_ZIP_HELP_LINE_2 = 0x238,           /* "transport orders to one of 3 common sets.  To select" */
  STR_ZIP_HELP_LINE_1 = 0x239,           /* "ZipOrd provides the ability to quickly set a fleet's" */
  STR_OF_YOUR_COLONISTS = 0x23a,         /* " of your colonists." */
  STR_SUPPORT_UP_TO = 0x23b,             /* "support up to " */
  STR_IT_WOULD = 0x23c,                  /* ", it would" */
  STR_IF_YOU_COLONIZE = 0x23d,           /* "If you were to colonize " */
  STR_YOUR_POPULATION_ON = 0x23e,        /* "Your population on " */
  STR_ENEMY_POPULATION_ON = 0x23f,       /* "The enemy population on " */
  STR_IS_APPROXIMATELY = 0x240,          /* " is approximately " */
  STR_IS_UNKNOWN = 0x241,                /* " is unknown" */
  STR_IS_UNINHABITED = 0x242,            /* " is uninhabited." */
  STR_WILL_KILL_OFF = 0x243,             /* " will kill off approximately " */
  STR_YOUR_COLONISTS_EACH_TURN = 0x244,  /* "your colonists each turn." */
  STR_OF_ALL_COLONISTS = 0x245,          /* "of all colonists you settle on it every turn." */
  STR_WILL_SUPPORT_UP_TO = 0x246,        /* " will support a population of up to " */
  STR_WITHIN_THE_RANGE_OF = 0x247,       /* "within the range of " */
  STR_OF = 0x248,                        /* " of" */
  STR_IS = 0x249,                        /* " is " */
  STR_TO = 0x24a,                        /* " to " */
  STR_ON = 0x24b,                        /* " on " */
  STR_MODIFY_THE = 0x24c,                /* "modify the " */
  STR_CURRENTLY_IS = 0x24d,              /* " currently is " */
  STR_UNKNOWN = 0x24e,                   /* "Unknown" */
  STR_IMMUNE_TO = 0x24f,                 /* "Your colonists are immune to" */
  STR_TO_THE_EFFECTS_OF = 0x250,         /* "to the effects of " */
  STR_COLONISTS_PREFER = 0x251,          /* "Your colonists prefer planets where the " */
  STR_IS_BETWEEN = 0x252,                /* " is between " */
  STR_AND = 0x253,                       /* " and " */
  STR_YOU_POSSESS_TECHNOLOGY = 0x254,    /* "You currently possess technology to" */
  STR_SHIP_NAME = 0x255,                 /* "Ship Name" */
  STR_PLANET = 0x256,                    /* "Planet: " */
  STR_SAMPLE_ID = 0x257,                 /* "9999": the widest id, sizes the pop-up */
  STR_SAMPLE_MINERAL_LINE = 0x258,       /* "Mineral Concentration: 0000000kT": sizes the pop-up */
  STR_Y_LABEL = 0x259,                   /* "Y: " */
  STR_X_LABEL = 0x25a,                   /* "X: " */
  STR_ID_LABEL = 0x25b,                  /* "ID: " */
  STR_FMT_PLAYER_NUMBER = 0x25c,         /* "Player #%d" */
  STR_NONE = 0x25d,                      /* "None" */
  STR_MINERAL_CONCENTRATION = 0x25e,     /* "Mineral Concentration: " */
  STR_ON_SURFACE = 0x25f,                /* "On Surface: " */
  STR_MINING_RATE = 0x260,               /* "Mining Rate: " */
  STR_ZIPORD = 0x261,                    /* "ZipOrd " */
  STR_DAMAGE = 0x336,                    /* "Damage" */
  STR_NO_FACTORIES = 0x4dc,              /* "Your race is incapable of building factories." (+1: mines) */
  STR_SAMPLE_DAMAGED = 0x4fc,            /* "99999@99%": sizes the damage column */
  STR_HOMEWORLD = 0x50f,                 /* " (HW)" */
  STR_THIRTY = 0x510,                    /* " (30)" */
  STR_MINE_FACTORY_FIRST = 0x0c9,        /* the first of the lines of the mines / factories text, one string per line */
  STR_RESOURCE_FIRST = 0x0e9             /* the first of the lines of the resource text */
};

/* ---- data of the data segment -------------------------------------------------------------------- */

enum {
  DS_POPUP_CLASS = 0x01d0,          /* "starspopup": window class of the pop-up */
  DS_CARGO_NAME_PTRS = 0x0464,      /* 6 near pointers: Ironium, Boranium, Germanium, Colonists, Fuel, Resources */
  DS_ENV_NAME_PTRS = 0x048e,        /* 3 near pointers: Gravity, Temperature, Radiation */
  DS_FMT_PTR_HUNDREDS = 0x0bbc,     /* near pointer to the format "%d00" (a count in hundreds of colonists) */
  DS_FMT_KT = 0x0bd6,               /* "%ldkT" */
  DS_FMT_KT_PER_YEAR = 0x0bdc,      /* "%ldkT/yr" */
  DS_STR_HASH = 0x0be5,             /* "#": the column title of the ship counts */
  DS_FMT_DAMAGED = 0x0be7,          /* "%d@%d%%": ships damaged @ damage percent */
  DS_STR_PLAYER_16 = 0x0bef,        /* "Player #16": the widest player line */
  DS_STR_DEEP_SPACE = 0x0bfa,       /* "Deep Space" */
  DS_STR_PERIOD_A = 0x0c05,         /* ".": the compiler keeps one copy of the string per use, a to f are the six uses (24:0x116a a to d, 24:0x18a4 e and f) */
  DS_STR_PERIOD_B = 0x0c07,
  DS_STR_PERIOD_C = 0x0c09,
  DS_STR_PERIOD_D = 0x0c0b,
  DS_FMT_PERCENT_PERIOD = 0x0c0d,   /* "%d%%.  " */
  DS_STR_PERIOD_E = 0x0c15,
  DS_FMT_HUNDREDS = 0x0c17,         /* "%d00" */
  DS_STR_PERIOD_F = 0x0c1c,
  DS_STR_FACTORY = 0x0c1e,          /* "Factory" */
  DS_STR_FACTORIES = 0x0c26,        /* "Factories" */
  DS_STR_MINE = 0x0c30,             /* "Mine" */
  DS_STR_MINES = 0x0c35,            /* "Mines" */
  DS_FMT_NAME_INFO = 0x0c3b,        /* "%s Info" */
  DS_FMT_COUNT_NOUN = 0x0c43,       /* "%d %s" */
  DS_SELECTED_PLACE_X = 0x4b7c,     /* the x word of the selected place g_selected_place_xy */
  DS_SELECTED_PLACE_Y = 0x4b7e,     /* its y word */
  DS_DESIGN_IS_STARBASE_MAYBE = 0x5a50, /* set by paint_info_popup for the design panels: 1 when the first word of the record is 0x20 or more (a starbase hull? read by 25:0x185e) */
  DS_PANEL_DESIGN_FLAG_MAYBE = 0x564a,  /* set by show_info_popup: 0 for kind 11, 1 for kind 14 (read by 25:0x450c) */
  DS_PANEL_DESIGN_PTR_MAYBE = 0x53e6    /* set by show_info_popup: the far pointer of the descriptor (INFO_ID), read by 25:0x450c */
};
#define DS_TEXT_BUFFER_OFF 0x5866     /* DS:0x5866, the address of g_text_buffer */

/* A menu item whose two words are all ones is a separator. */
#define ITEM_SEPARATOR 0xffffffffu

/* Other named values. */
enum {
  COLOR_WHITE = 0x00ffffff,
  COLOR_BLACK = 0,
  COLOR_RED = 0x000000ff,
  PLAYER_ROW_SIZE = 0xc0,      /* sizeof(struct Player) */
  RACE_VALUE_NO_FACTORIES = 8, /* attribute PLAYER_ATTR_RACE == 8: the race can build no factories / mines the normal way */
  GAME_FLAG_BOTH_BUTTONS_MASK = 0xc0, /* DS:0x0791 bits 6 and 7 */
  UI_FLAG_POPUP_REFRESH = 0x08,       /* DS:0x0791 bit 3: closing the pop-up asks the main window to repaint */
  FRAME_FLAG_REPAINT = 0x04,          /* DS:0x53f1 bit 2 */
  AMPERSAND = 0x26,
  SM_CXSCREEN = 0,
  SM_CYSCREEN = 1,
  MENU_ID_BASE = 0x3a98,       /* the command ids of the pop-up menu items: MENU_ID_BASE + index */
  MENU_ID_LIMIT = 0x3afc,
  MENU_LABEL_SEPARATOR = 0xff, /* a label whose first byte is 0xff and second byte 0 is a separator */
  ITEM_DEEP_SPACE = 0x1000,    /* flag word of a menu item: the text is "Deep Space" */
  ITEM_STRING_ID = 0x4000,     /* the id is a string of the string table */
  ITEM_FLEET_MAYBE = 0x2000,
  ITEM_TARGET_MAYBE = 0x8000,
  SELECTED_PER_ITEM = 0xfffe,  /* the selection argument of popup_menu_choose: -2, the checks come from the items */
  DESIGN_STARBASE_HULL_MIN = 0x20, /* first word of a design record at or above this: DS:0x5a50 = 1 */
  POPUP_MARGIN = 4,
  PLAYER_OWNER_NONE = 0xffff,
  ENV_NO_LIMIT = 0xffff,       /* an environment limit of the descriptor that does not apply (the race is immune to the value) */
  MEASURE_TEXT_WIDTH = 0xc8,   /* the width given to the text layout functions when only measuring (the pop-up is not wider than this) */
  TECH_PANEL_WIDTH = 0x158,    /* the tech item panel: width 0x158 (+0x28 with a large font), height 12 lines + the 10-point cell + 0x4e */
  TECH_PANEL_WIDE_EXTRA = 0x28,
  TECH_PANEL_SMALL_FONT_LIMIT = 14,
  TECH_PANEL_HEIGHT_EXTRA = 0x4e,
  DESIGN_PANEL_WIDTH = 0x154,  /* the design panel: width 0x154, height 7 lines + 0x13a (+3 lines when both mouse buttons are shown) */
  DESIGN_PANEL_HEIGHT_EXTRA = 0x13a,
  EMPTY_PANEL_WIDTH = 0x78,
  EMPTY_PANEL_HEIGHT = 0x50
};

/* DS:0x4c84 holds the last choice of the pop-up menu (the command id minus 0x3a98); seg04.c names it g_recent_file_slot
 * for the same command id range. */
#define g_popup_choice_maybe g_recent_file_slot

/* ---- helpers ---------------------------------------------------------------------------------- */

/* PUSH on the emulated stack: the variable arguments of _wsprintf. */
static inline void push_word(uint16_t v) {
  rt_sp = (uint16_t)(rt_sp - 2);
  memcpy(LIN(MK_FP(rt_ss, rt_sp)), &v, 2);
}
static inline void pop_words(uint16_t n) { rt_sp = (uint16_t)(rt_sp + 2 * n); }
static inline uint32_t ds_far(uint16_t off) { return MK_FP(MEM_SEL_DGROUP, off); }
/* Address of the frame slot at BP+off: as a near pointer (what the text helpers take) and as a far one. */
static inline uint16_t bp_near(int16_t off) { return (uint16_t)(rt_bp + off); }
static inline uint32_t bp_far(int16_t off) { return MK_FP(rt_ss, bp_near(off)); }
static inline uint16_t line_height(void) { return g_text_line_height; }
/* The length of the NUL-terminated string at sel:off (the original scans with REPNE SCASB). */
static uint16_t string_length(uint16_t sel, uint16_t off) {
  uint16_t n = 0;
  while (*(const uint8_t *)LIN(MK_FP(sel, (uint16_t)(off + n))) != 0) n++;
  return n;
}
static inline uint16_t ds_string_length(uint16_t off) { return string_length(MEM_SEL_DGROUP, off); }

/* _wsprintf(out, fmt, args) with the argument words pushed on the emulated stack, args[0] first. */
static uint16_t format_text(uint32_t out, uint32_t fmt, uint16_t nwords, const uint16_t *args) {
  for (uint16_t i = nwords; i > 0; i--) push_word(args[i - 1]);
  uint16_t written = (uint16_t)_wsprintf(out, fmt, MK_FP(rt_ss, rt_sp));
  pop_words(nwords);
  return written;
}
static inline uint16_t format_ds1(uint16_t fmt, uint16_t value) {
  return format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(fmt), 1, (const uint16_t[]){value});
}
static inline uint16_t format_ds2(uint16_t fmt, uint16_t a, uint16_t b) {
  return format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(fmt), 2, (const uint16_t[]){a, b});
}
static inline uint16_t format_ds_long(uint16_t fmt, uint32_t value) {
  return format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(fmt), 2, (const uint16_t[]){(uint16_t)value, (uint16_t)(value >> 16)});
}
static inline int16_t text_extent(uint16_t hdc, uint32_t str, uint16_t count) {
  return (int16_t)(uint16_t)GetTextExtent(hdc, str, (int16_t)count);
}

/* The segment 8 helpers (not decompiled yet). */
/* 8:0x8df4: the string of the string table with that id, decoded into a buffer of the data segment; returns its near pointer. */
static inline uint16_t string_text(uint16_t id) { return (uint16_t)CALL_ORIG(8, 0x8df4, FAR_CDECL, 1, id); }
/* 8:0x8dc0: copies the string of that id to the DS buffer dest; returns its length. */
static inline uint16_t copy_string_text(uint16_t id, uint16_t dest) { return (uint16_t)CALL_ORIG(8, 0x8dc0, FAR_CDECL, 2, id, dest); }
/* 8:0x90b0: one piece of text at the cursor: draws str (len characters, 0 = all) at (*x, y) when draw is non-zero and moves *x on by its width. */
static inline uint16_t draw_text_advance(uint16_t hdc, uint16_t x_slot, int16_t y, uint16_t str, uint16_t len, uint16_t draw) {
  return (uint16_t)CALL_ORIG(8, 0x90b0, FAR_CDECL, 6, hdc, x_slot, (uint16_t)y, str, len, draw);
}
/* 8:0x90fe: wrapped text: lays out str (len characters, 0 = all) from the cursor (*x, *y) in lines of
 * `width` pixels with the left margin `margin`; draws it when draw is non-zero; *max_x (when not 0) is raised to the widest line;
 * `newline_first` starts on a new line. */
static inline void draw_text_wrapped(uint16_t hdc, uint16_t x_slot, uint16_t y_slot, uint16_t str, uint16_t len, uint16_t margin,
                                     uint16_t width, uint16_t max_x_slot, uint16_t newline_first, uint16_t draw) {
  (void)CALL_ORIG(8, 0x90fe, FAR_CDECL, 10, hdc, x_slot, y_slot, str, len, margin, width, max_x_slot, newline_first, draw);
}
/* 8:0x9072: str (len characters, 0 = all) centred on x at height y. */
static inline void draw_text_centered(uint16_t hdc, int16_t x, int16_t y, uint16_t str, uint16_t len) {
  (void)CALL_ORIG(8, 0x9072, FAR_CDECL, 5, hdc, (uint16_t)x, (uint16_t)y, str, len);
}
/* 8:0x9398: str (len characters, 0 = all, 0xffff = "None") right-aligned at x, y; a field of `field_width` is cleared first. */
static inline void draw_text_right_aligned(uint16_t hdc, int16_t x, int16_t y, uint16_t str, uint16_t len, uint16_t field_width) {
  (void)CALL_ORIG(8, 0x9398, FAR_CDECL, 6, hdc, (uint16_t)x, (uint16_t)y, str, len, field_width);
}
/* 7:0x1cce: the name of the planet with that id (a near pointer to a DS buffer). */
static inline uint16_t planet_name(uint16_t id) { return (uint16_t)CALL_ORIG(7, 0x1cce, FAR_CDECL, 1, id); }
/* 9:0x3e72: the text of an environment value (axis, value), for instance "35C". */
static inline uint16_t environment_text(uint16_t axis, uint16_t value) { return (uint16_t)CALL_ORIG(9, 0x3e72, FAR_CDECL, 2, axis, value); }
/* 28:0x21ee: the attribute `index` of the player record at DS offset row. */
static inline uint16_t player_attribute(uint16_t row, uint16_t index) { return (uint16_t)CALL_ORIG(28, 0x21ee, FAR_CDECL, 2, row, index); }
static inline uint16_t current_player_row(void) { return (uint16_t)((uint16_t)(g_current_player * PLAYER_ROW_SIZE) + DS_PLAYERS); }
/* 7:0x0b84: the name of the player (singular, as the pop-up shows it). */
static inline uint16_t player_name(uint16_t player) { return (uint16_t)CALL_ORIG(7, 0x0b84, FAR_CDECL, 6, player, 1, 1, 1, 0, 0); }
/* 7:0x3b9e: copies the name of ship design `design` of the player `owner` to the frame buffer at `dest`. */
static inline void design_name(uint16_t owner, uint16_t design, uint16_t dest) { (void)CALL_ORIG(7, 0x3b9e, FAR_CDECL, 3, owner, design, dest); }
/* 7:0x1902 / 7:0x1a70: the names the pop-up menu shows for the flag 0x2000 and 0x8000 items. */
static inline uint16_t menu_fleet_name_maybe(uint16_t id) { return (uint16_t)CALL_ORIG(7, 0x1902, FAR_CDECL, 1, id); }
static inline uint16_t menu_target_name_maybe(uint16_t id) { return (uint16_t)CALL_ORIG(7, 0x1a70, FAR_CDECL, 1, id); }
/* 7:0x01e4: the far pointer (DX:AX) to the planet record with that id. */
static inline uint32_t planet_record(uint16_t id) { return (uint32_t)CALL_ORIG(7, 0x01e4, FAR_CDECL, 1, id); }
/* 7:0x0358: fills the record at the near pointer `copy` with the planet as the current player sees it. */
static inline void planet_view_copy(uint16_t id, uint16_t copy) { (void)CALL_ORIG(7, 0x0358, FAR_CDECL, 2, id, copy); }
/* 7:0x46fc: the population growth of the planet record at the far pointer off:seg in a year (dword). */
static inline uint32_t planet_growth(uint16_t off, uint16_t seg) { return (uint32_t)CALL_ORIG(7, 0x46fc, FAR_CDECL, 3, off, seg, 0); }
/* 9:0x474c: the habitability in percent of the planet record at off:seg for the player. */
static inline int16_t planet_habitability(uint16_t off, uint16_t seg, uint16_t player) {
  return (int16_t)(uint16_t)CALL_ORIG(9, 0x474c, FAR_CDECL, 3, off, seg, player);
}
/* 9:0x48cc: the population (dword) the planet supports for the player. */
static inline uint32_t planet_capacity(uint16_t id, uint16_t player) { return (uint32_t)CALL_ORIG(9, 0x48cc, FAR_CDECL, 2, id, player); }
/* 25:0x185e / 25:0x2ed6: draw the picture and the details of a ship design into the rectangle at the near pointer. */
static inline void draw_design_picture(uint16_t hwnd, uint16_t hdc, uint16_t rect) { (void)CALL_ORIG(25, 0x185e, FAR_CDECL, 4, hwnd, hdc, rect, 0xffff); }
static inline void draw_design_details(uint16_t hwnd, uint16_t hdc, uint16_t rect) { (void)CALL_ORIG(25, 0x2ed6, FAR_CDECL, 4, hwnd, hdc, 0xffff, rect); }
/* 25:0x450c: prepares the design panel for the record DS:0x53e6. 31:0x61b0: refreshes the main window. */
static inline void prepare_design_panel(void) { (void)CALL_ORIG(25, 0x450c, FAR_CDECL, 0); }
static inline void refresh_main_window(void) { (void)CALL_ORIG(31, 0x61b0, FAR_CDECL, 0); }

/* The functions of this segment and of segment 27 that mirror a frame, reached through rt_call_far. */
enum {
  OFF_PAINT_INFO_POPUP = 0x00ca,
  OFF_LAYOUT_ENVIRONMENT = 0x116a,
  OFF_LAYOUT_COLONISTS = 0x18a4,
  OFF_LAYOUT_ZIP_HELP = 0x208c,
  OFF_LAYOUT_MINES = 0x23ba,
  OFF_LAYOUT_RESOURCES = 0x25a2,
  OFF_LAYOUT_TOOLTIP = 0x2738,
  OFF_PAINT_TECH_ITEM = 0x1e06 /* in segment 27 */
};
static inline uint32_t call_seg24(uint16_t off, const uint16_t *args, uint16_t nargs) {
  return rt_call_far(MK_FP(SEG_SEL(24), off), FAR_CDECL, args, nargs);
}
static inline uint32_t call_seg27(uint16_t off, const uint16_t *args, uint16_t nargs) {
  return rt_call_far(MK_FP(SEG_SEL(27), off), FAR_CDECL, args, nargs);
}
/* The result of a layout function: width + 4 in the low word, height + 4 in the high word. */
static inline uint32_t layout_result(uint16_t width, uint16_t height) {
  return ((uint32_t)(uint16_t)(height + POPUP_MARGIN) << 16) | (uint16_t)(width + POPUP_MARGIN);
}
/* The result of the layout functions that end below the last line: width + 4, y + one line + 2. */
static inline uint32_t layout_result_below(uint16_t width, uint16_t y) {
  return ((uint32_t)(uint16_t)(y + g_text_line_height + 2) << 16) | (uint16_t)(width + POPUP_MARGIN);
}

/* ---- 24:0x0000 POPUPWNDPROC ----------------------------------------------------------------------- */

#define POPUPWND_FRAME 0x28
#define PAINTSTRUCT_AT (-0x28) /* PAINTSTRUCT of WM_PAINT, 32 bytes */
#define CLIENT_RECT_AT (-8)    /* RECT of WM_ERASEBKGND */

/* POPUPWNDPROC: 24:0x0000 (Ghidra and export name POPUPWNDPROC, FUN_10b8_0000), 201 bytes. ENTER 0x28,0;
 * PUSH SI. FAR PASCAL window procedure (hwnd, msg, wParam, lParam) of the class "starspopup".
 * WM_PAINT (0x0f): BeginPaint, paint_info_popup(hwnd, hdc), EndPaint, returns 0.
 * WM_ERASEBKGND (0x14): for the kinds 9 (tech item), 11 and 14 (designs), whose painters draw the
 * whole client area themselves, fills the client rectangle with the button face brush and returns 1;
 * for the other kinds the default procedure.
 * WM_LBUTTONUP (0x202) and WM_RBUTTONUP (0x205): closes the pop-up if one is open: destroys the window
 * (DS:0x0bbe), clears it and the descriptor kind, releases the mouse and, when DS:0x0791 bit 3 is set,
 * sets bit 2 of DS:0x53f1 and calls 31:0x61b0 (the main window refresh). Returns 0.
 * WM_CREATE returns 0; every other message is answered by DefWindowProc. DX:AX is the result. */
DECOMP(24, 0x0000, POPUPWNDPROC, FAR_PASCAL)
uint32_t POPUPWNDPROC(uint16_t hwnd, uint16_t msg, uint16_t wparam, uint32_t lparam)
{
  FRAME_ENTER_REGS(POPUPWND_FRAME, 1);
  uint32_t result = 0;
  if (msg == WM_CREATE) {
    result = 0;
  } else if (msg == WM_PAINT) {
    uint16_t hdc = BeginPaint(hwnd, bp_far(PAINTSTRUCT_AT));
    (void)call_seg24(OFF_PAINT_INFO_POPUP, (const uint16_t[]){hwnd, hdc}, 2);
    EndPaint(hwnd, bp_far(PAINTSTRUCT_AT));
  } else if (msg == WM_ERASEBKGND) {
    uint16_t kind = INFO_WORD(INFO_KIND);
    if (kind == INFO_KIND_TECH_ITEM || kind == INFO_KIND_DESIGN || kind == INFO_KIND_DESIGN_B) {
      GetClientRect(hwnd, bp_far(CLIENT_RECT_AT));
      (void)FillRect(wparam, bp_far(CLIENT_RECT_AT), g_brush_btnface);
      result = 1;
    } else {
      result = DefWindowProc(hwnd, msg, wparam, lparam);
    }
  } else if (msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
    if (g_hwnd_popup_capture_maybe != 0) {
      (void)DestroyWindow(g_hwnd_popup_capture_maybe);
      g_hwnd_popup_capture_maybe = 0;
      INFO_WORD(INFO_KIND) = 0;
      ReleaseCapture();
      if ((g_ui_flags_b_maybe & UI_FLAG_POPUP_REFRESH) != 0) {
        g_frame_flags_b_maybe = (uint8_t)(g_frame_flags_b_maybe | FRAME_FLAG_REPAINT);
        refresh_main_window();
      }
    }
  } else {
    result = DefWindowProc(hwnd, msg, wparam, lparam);
  }
  FRAME_LEAVE();
  return result;
}
#undef POPUPWND_FRAME
#undef PAINTSTRUCT_AT
#undef CLIENT_RECT_AT

/* ---- 24:0x00ca paint_info_popup ------------------------------------------------------------------ */

#define PAINT_FRAME 0x46
#define PAINT_RECT_AT (-0x1e)      /* the client RECT, handed to GetClientRect and to 25:0x185e / 25:0x2ed6 */
#define PAINT_NAME_BUF_AT (-0x46)  /* the design name buffer of the fleet list, handed to 7:0x3b9e */
#define PAINT_RECT BP_LOCAL(RECT16, PAINT_RECT_AT)

/* Kind 1: the three lines of a planet mineral: the mineral name centred on top, then the labels
 * "Mineral Concentration:", "On Surface:" and "Mining Rate:" right-aligned in front of their values. */
static void paint_mineral_panel(uint16_t hdc)
{
  uint16_t mineral = NEAR_AT(uint16_t, (uint16_t)(DS_CARGO_NAME_PTRS + 2 * INFO_WORD(INFO_ID)));
  draw_text_centered(hdc, (int16_t)(PAINT_RECT.right >> 1), POPUP_MARGIN, mineral, 0);
  (void)SelectObject(hdc, g_font_arial_8pt);
  uint16_t label = string_text(STR_MINERAL_CONCENTRATION);
  int16_t value_x = (int16_t)(uint16_t)(text_extent(hdc, ds_far(label), ds_string_length(label)) + POPUP_MARGIN);
  uint16_t line_text = string_text(STR_ON_SURFACE);
  draw_text_right_aligned(hdc, value_x, (int16_t)(line_height() + POPUP_MARGIN), line_text, 0, 0);
  uint16_t shown;
  if ((int16_t)INFO_WORD(INFO_ARG_C) >= 0) {
    shown = format_ds_long(DS_FMT_KT, INFO_DWORD(INFO_ARG_A));
  } else {
    uint16_t unknown = string_text(STR_UNKNOWN);
    memcpy(LINN(DS_TEXT_BUFFER_OFF), LINN(unknown), (size_t)ds_string_length(unknown) + 1);
    shown = ds_string_length(DS_TEXT_BUFFER_OFF);
  }
  (void)TextOut(hdc, value_x, (int16_t)(line_height() + POPUP_MARGIN), ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
  int16_t second_y = (int16_t)(uint16_t)((line_height() + 2) * 2);
  uint16_t line_text_2 = string_text(STR_MINERAL_CONCENTRATION);
  draw_text_right_aligned(hdc, value_x, second_y, line_text_2, 0, 0);
  if ((int32_t)INFO_DWORD(INFO_ARG_E) > 0) {
    shown = format_ds_long(g_fmt_ptr_dword_maybe, INFO_DWORD(INFO_ARG_E));
    if (INFO_DWORD(INFO_ARG_6) != 0) {
      uint16_t suffix = string_text((int32_t)INFO_DWORD(INFO_ARG_E) >= 30 ? STR_HOMEWORLD : STR_THIRTY);
      uint16_t more = format_text(ds_far((uint16_t)(DS_TEXT_BUFFER_OFF + shown)), ds_far(suffix), 0, NULL);
      shown = (uint16_t)(shown + more);
    }
  } else {
    uint16_t unknown = string_text(STR_UNKNOWN);
    shown = format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(unknown), 0, NULL);
  }
  (void)TextOut(hdc, value_x, second_y, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
  if ((int16_t)INFO_WORD(INFO_ARG_14) >= 0) {
    int16_t third_y = (int16_t)(uint16_t)(3 * line_height() + POPUP_MARGIN);
    uint16_t rate_label = string_text(STR_MINING_RATE);
    draw_text_right_aligned(hdc, value_x, third_y, rate_label, 13, 0); /* 13 = the length of "Mining Rate: " */
    shown = format_ds_long(DS_FMT_KT_PER_YEAR, INFO_DWORD(INFO_ARG_12));
    (void)TextOut(hdc, value_x, third_y, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
  }
}

/* Kind 3: the ships of a fleet: a column of design names, the ship count and, when INFO_ARG_6 is
 * set, the damaged ships ("n@p%") in red. When the window is too short for the columns only "None". */
static void paint_fleet_panel(uint16_t hdc)
{
  int16_t height = (int16_t)(uint16_t)(PAINT_RECT.bottom - PAINT_RECT.top);
  if (height < (int16_t)(uint16_t)(2 * line_height())) {
    uint16_t len = copy_string_text(STR_NONE, DS_TEXT_BUFFER_OFF);
    (void)TextOut(hdc, POPUP_MARGIN, POPUP_MARGIN, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)len);
    (void)SelectObject(hdc, g_font_arial_8pt);
    return;
  }
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  uint16_t title = string_text(STR_SHIP_NAME);
  (void)TextOut(hdc, POPUP_MARGIN, POPUP_MARGIN, ds_far(title), 9); /* 9 = the length of "Ship Name" */
  int16_t count_x = (int16_t)(uint16_t)(PAINT_RECT.right - INFO_WORD(INFO_ARG_8) - POPUP_MARGIN);
  draw_text_right_aligned(hdc, count_x, POPUP_MARGIN, DS_STR_HASH, 1, 0);
  if (INFO_WORD(INFO_ARG_8) != 0) {
    uint16_t len = copy_string_text(STR_DAMAGE, DS_TEXT_BUFFER_OFF);
    draw_text_right_aligned(hdc, (int16_t)(PAINT_RECT.right - POPUP_MARGIN), POPUP_MARGIN, DS_TEXT_BUFFER_OFF, len, 0);
    (void)PatBlt(hdc, POPUP_MARGIN, (int16_t)(line_height() + 2), (int16_t)(PAINT_RECT.right - 8), 1, BLACKNESS);
  }
  (void)SelectObject(hdc, g_font_arial_8pt);
  uint16_t y = (uint16_t)(line_height() + POPUP_MARGIN);
  for (uint16_t design = 0; design < 16; design++) {
    uint16_t fleet_off = INFO_WORD(INFO_ID), fleet_seg = INFO_WORD(INFO_ARG_4);
    if ((int16_t)FAR_REC(struct Fleet, fleet_seg, fleet_off)->ship_count[design] <= 0) continue;
    if (INFO_WORD(INFO_ARG_6) != 0) {
      (void)SetTextColor(hdc, FAR_REC(struct Fleet, fleet_seg, fleet_off)->damage[design] != 0 ? COLOR_RED : COLOR_BLACK);
    }
    uint8_t owner_bits = ((const uint8_t *)&FAR_REC(struct Fleet, fleet_seg, fleet_off)->id)[1];
    design_name(map_object_owner(owner_bits), design, bp_near(PAINT_NAME_BUF_AT));
    uint16_t name_len = string_length(rt_ss, bp_near(PAINT_NAME_BUF_AT));
    (void)TextOut(hdc, POPUP_MARGIN, (int16_t)y, bp_far(PAINT_NAME_BUF_AT), (int16_t)name_len);
    uint16_t ships = FAR_REC(struct Fleet, fleet_seg, fleet_off)->ship_count[design];
    uint16_t shown = format_ds1(g_fmt_ptr_a_maybe, ships);
    draw_text_right_aligned(hdc, count_x, (int16_t)y, DS_TEXT_BUFFER_OFF, shown, 0);
    if (INFO_WORD(INFO_ARG_6) != 0 && FAR_REC(struct Fleet, fleet_seg, fleet_off)->damage[design] != 0) {
      uint16_t damage = FAR_REC(struct Fleet, fleet_seg, fleet_off)->damage[design];
      uint16_t damaged_ships = (uint16_t)(((int32_t)(int16_t)ships * (damage & FLEET_DAMAGE_PCT_MASK)) / 100);
      if ((int16_t)damaged_ships <= 0) damaged_ships = 1;
      uint16_t damage_each = (uint16_t)((damage / 5) >> FLEET_DAMAGE_SHIFT);
      if (damage_each == 0) damage_each = 1;
      shown = format_ds2(DS_FMT_DAMAGED, damaged_ships, damage_each);
      draw_text_right_aligned(hdc, (int16_t)(PAINT_RECT.right - POPUP_MARGIN), (int16_t)y, DS_TEXT_BUFFER_OFF, shown, 0);
    }
    y = (uint16_t)(y + line_height());
  }
  if (INFO_WORD(INFO_ARG_6) != 0) (void)SetTextColor(hdc, COLOR_BLACK);
}

/* Kind 4: the selected place: the labels "Planet:", "ID:", "X:" and "Y:" with the name, the id + 1
 * and the position of the planet DS:0x4b84 / the point DS:0x4b7c. */
static void paint_place_panel(uint16_t hdc)
{
  uint16_t planet_label = string_text(STR_PLANET);
  int16_t value_x = (int16_t)(uint16_t)(text_extent(hdc, ds_far(planet_label), ds_string_length(planet_label)) + POPUP_MARGIN);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  draw_text_right_aligned(hdc, value_x, POPUP_MARGIN, planet_label, ds_string_length(planet_label), 0);
  int16_t second_y = (int16_t)(line_height() + POPUP_MARGIN);
  int16_t third_y = (int16_t)(uint16_t)((line_height() + 2) * 2);
  int16_t fourth_y = (int16_t)(uint16_t)(3 * line_height() + POPUP_MARGIN);
  uint16_t id_label = string_text(STR_ID_LABEL);
  draw_text_right_aligned(hdc, value_x, second_y, id_label, 0, 0);
  uint16_t x_label = string_text(STR_X_LABEL);
  draw_text_right_aligned(hdc, value_x, third_y, x_label, 0, 0);
  uint16_t y_label = string_text(STR_Y_LABEL);
  draw_text_right_aligned(hdc, value_x, fourth_y, y_label, 0, 0);
  uint16_t name = planet_name(g_selected_planet);
  (void)SelectObject(hdc, g_font_arial_8pt);
  (void)TextOut(hdc, value_x, POPUP_MARGIN, ds_far(name), (int16_t)ds_string_length(name));
  uint16_t shown = format_ds1(g_fmt_ptr_a_maybe, (uint16_t)(g_selected_planet + 1));
  (void)TextOut(hdc, value_x, second_y, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
  shown = format_ds1(g_fmt_ptr_a_maybe, NEAR_AT(uint16_t, DS_SELECTED_PLACE_X));
  (void)TextOut(hdc, value_x, third_y, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
  shown = format_ds1(g_fmt_ptr_a_maybe, NEAR_AT(uint16_t, DS_SELECTED_PLACE_Y));
  (void)TextOut(hdc, value_x, fourth_y, ds_far(DS_TEXT_BUFFER_OFF), (int16_t)shown);
}

/* A ship design panel (kinds 11 and 14): the picture and the details are drawn by 25:0x185e and
 * 25:0x2ed6; this adds the design name centred on top. */
static void paint_design_panel(uint16_t hwnd, uint16_t hdc)
{
  uint16_t design_off = INFO_WORD(INFO_ID), design_seg = INFO_WORD(INFO_ARG_4);
  NEAR_AT(uint16_t, DS_DESIGN_IS_STARBASE_MAYBE) = (int16_t)FAR_REC(uint16_ua, design_seg, design_off)[0] >= DESIGN_STARBASE_HULL_MIN ? 1 : 0;
  draw_design_picture(hwnd, hdc, bp_near(PAINT_RECT_AT));
  PAINT_RECT.top = (int16_t)(uint16_t)(line_height() + 0x132);
  PAINT_RECT.left = (int16_t)(uint16_t)(PAINT_RECT.left + 6);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  (void)SetBkMode(hdc, TRANSPARENT);
  design_off = INFO_WORD(INFO_ID);
  design_seg = INFO_WORD(INFO_ARG_4);
  /* the design name is the string at +8 of the record, copied to the text buffer */
  uint16_t name_off = (uint16_t)(design_off + offsetof(struct ShipDesign, name));
  memcpy(LINN(DS_TEXT_BUFFER_OFF), LIN(MK_FP(design_seg, name_off)), (size_t)string_length(design_seg, name_off) + 1);
  draw_text_centered(hdc, (int16_t)(((int16_t)(uint16_t)(PAINT_RECT.right - 0x4c) >> 1) + 0x4c), 6, DS_TEXT_BUFFER_OFF, 0);
  draw_design_details(hwnd, hdc, bp_near(PAINT_RECT_AT));
}

/* paint_info_popup: 24:0x00ca (Ghidra FUN_10b8_00ca), 2132 bytes with the code the inventory leaves out
 * (24:0x0131 to 0x091d: the jump table and the cases). ENTER 0x46,0; PUSH DI; PUSH SI. FAR CDECL
 * (hwnd, hdc). Paints the pop-up window for the descriptor DS:0x0BC0: sets a white background, black
 * text, opaque mode and the bold font, fetches the client rectangle (BP-0x1e), draws the page of the
 * descriptor's kind (see INFO_KIND_*) and restores the old colours, mode and font. Kind 13 draws nothing; an
 * unknown kind none either. The returned DX:AX is the old background colour, from the last API call. */
DECOMP(24, 0x00ca, paint_info_popup, FAR_CDECL)
uint32_t paint_info_popup(uint16_t hwnd, uint16_t hdc)
{
  FRAME_ENTER_REGS(PAINT_FRAME, 2);
  uint32_t old_bk_color = SetBkColor(hdc, COLOR_WHITE);
  uint32_t old_text_color = SetTextColor(hdc, COLOR_BLACK);
  int16_t old_bk_mode = SetBkMode(hdc, OPAQUE);
  uint16_t old_font = SelectObject(hdc, g_font_arial_bold_8pt);
  GetClientRect(hwnd, bp_far(PAINT_RECT_AT));
  switch (INFO_WORD(INFO_KIND)) {
  case INFO_KIND_PLANET_MINERAL:
    paint_mineral_panel(hdc);
    break;
  case INFO_KIND_PLAYER: {
    uint16_t player = INFO_WORD(INFO_ID);
    uint16_t name = player_name(player);
    draw_text_centered(hdc, (int16_t)(PAINT_RECT.right >> 1), POPUP_MARGIN, name, 0);
    uint16_t format = string_text(STR_FMT_PLAYER_NUMBER);
    uint16_t written = format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(format), 1, (const uint16_t[]){(uint16_t)(INFO_WORD(INFO_ID) + 1)});
    draw_text_centered(hdc, (int16_t)(PAINT_RECT.right >> 1), (int16_t)(line_height() + POPUP_MARGIN), DS_TEXT_BUFFER_OFF, written);
    break;
  }
  case INFO_KIND_FLEET:
    paint_fleet_panel(hdc);
    break;
  case INFO_KIND_PLACE:
    paint_place_panel(hdc);
    break;
  case INFO_KIND_ENVIRONMENT:
    (void)call_seg24(OFF_LAYOUT_ENVIRONMENT, (const uint16_t[]){hdc, 1}, 2);
    break;
  case INFO_KIND_ZIP_ORDER_HELP:
    (void)call_seg24(OFF_LAYOUT_ZIP_HELP, (const uint16_t[]){hdc, (uint16_t)(PAINT_RECT.right >> 1), 1}, 3);
    break;
  case INFO_KIND_COLONISTS:
    (void)call_seg24(OFF_LAYOUT_COLONISTS, (const uint16_t[]){hdc, 1}, 2);
    break;
  case INFO_KIND_MINE_FACTORY:
    (void)call_seg24(OFF_LAYOUT_MINES, (const uint16_t[]){hdc, (uint16_t)PAINT_RECT.right, 1}, 3);
    break;
  case INFO_KIND_TECH_ITEM:
    (void)call_seg27(OFF_PAINT_TECH_ITEM, (const uint16_t[]){hdc, (uint16_t)PAINT_RECT.right, (uint16_t)PAINT_RECT.bottom, DS_INFO + INFO_ID}, 4);
    break;
  case INFO_KIND_TEXT:
    (void)call_seg24(OFF_LAYOUT_TOOLTIP, (const uint16_t[]){hdc, (uint16_t)PAINT_RECT.right, 1}, 3);
    break;
  case INFO_KIND_DESIGN:
  case INFO_KIND_DESIGN_B:
    paint_design_panel(hwnd, hdc);
    break;
  case INFO_KIND_RESOURCES:
    (void)call_seg24(OFF_LAYOUT_RESOURCES, (const uint16_t[]){hdc, (uint16_t)PAINT_RECT.right, 1}, 3);
    break;
  default:
    break;
  }
  (void)SelectObject(hdc, old_font);
  (void)SetBkMode(hdc, old_bk_mode);
  (void)SetTextColor(hdc, old_text_color);
  uint32_t returned = SetBkColor(hdc, old_bk_color);
  FRAME_LEAVE();
  return returned;
}
#undef PAINT_FRAME
#undef PAINT_RECT_AT
#undef PAINT_NAME_BUF_AT
#undef PAINT_RECT

/* ---- 24:0x091e show_info_popup ------------------------------------------------------------------- */

#define SHOW_FRAME 0x42
#define SHOW_POINT_AT (-0x14)     /* POINT16: the mouse position, handed to ClientToScreen */
#define SHOW_WIDTH_AT (-6)        /* window width: stays unset (stack content) for an unknown kind */
#define SHOW_HEIGHT_AT (-0x0e)    /* window height, likewise */
#define SHOW_NAME_BUF_AT (-0x42)  /* design name buffer, handed to 7:0x3b9e */
#define SHOW_POINT BP_LOCAL(POINT16, SHOW_POINT_AT)
#define SHOW_WIDTH BP_LOCAL(int16_t, SHOW_WIDTH_AT)
#define SHOW_HEIGHT BP_LOCAL(int16_t, SHOW_HEIGHT_AT)

static inline int16_t max_signed(int16_t a, int16_t b) { return a > b ? a : b; }

/* The size of the fleet list of kind 3: the widest design name, ship count and damage text, one line per
 * design with ships. Stores the width of the damage column in INFO_ARG_8 for the painter. */
static void measure_fleet_list(uint16_t hdc)
{
  SHOW_HEIGHT = (int16_t)(uint16_t)(line_height() + 8);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  uint16_t title = string_text(STR_SHIP_NAME);
  int16_t name_column = text_extent(hdc, ds_far(title), ds_string_length(title));
  (void)SelectObject(hdc, g_font_arial_8pt);
  int16_t count_column = 0, damage_column = 0;
  uint16_t design = 0;
  for (uint16_t slot = 0; slot < 0x20; slot = (uint16_t)(slot + 2), design++) {
    uint16_t fleet_off = INFO_WORD(INFO_ID), fleet_seg = INFO_WORD(INFO_ARG_4);
    if ((int16_t)FAR_REC(struct Fleet, fleet_seg, fleet_off)->ship_count[design] <= 0) continue;
    SHOW_HEIGHT = (int16_t)(uint16_t)(SHOW_HEIGHT + line_height());
    uint8_t owner_bits = ((const uint8_t *)&FAR_REC(struct Fleet, fleet_seg, INFO_WORD(INFO_ID))->id)[1];
    design_name(map_object_owner(owner_bits), design, bp_near(SHOW_NAME_BUF_AT));
    int16_t name_width = text_extent(hdc, bp_far(SHOW_NAME_BUF_AT), string_length(rt_ss, bp_near(SHOW_NAME_BUF_AT)));
    name_column = max_signed(name_width, name_column);
    uint16_t shown = format_ds1(g_fmt_ptr_a_maybe, FAR_REC(struct Fleet, INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ID))->ship_count[design]);
    count_column = max_signed(text_extent(hdc, ds_far(DS_TEXT_BUFFER_OFF), shown), count_column);
    fleet_off = INFO_WORD(INFO_ID);
    fleet_seg = INFO_WORD(INFO_ARG_4);
    if (INFO_WORD(INFO_ARG_6) != 0 && (FAR_REC(struct Fleet, fleet_seg, fleet_off)->damage[design] & ~FLEET_DAMAGE_PCT_MASK) != 0 &&
        damage_column == 0) {
      uint16_t sample = string_text(STR_SAMPLE_DAMAGED);
      damage_column = (int16_t)(uint16_t)(text_extent(hdc, ds_far(sample), ds_string_length(sample)) + POPUP_MARGIN);
    }
  }
  INFO_WORD(INFO_ARG_8) = (uint16_t)damage_column;
  SHOW_WIDTH = (int16_t)(uint16_t)(damage_column + count_column + name_column + 0x10);
}

/* Moves the window corner `pos` (from the mouse position minus the window size) so that the window of
 * `size` fits the screen along the axis `metric` (GetSystemMetrics 0 = width, 1 = height): not past
 * the right / bottom edge and not below 0. The original asks for the screen size up to four times. */
static int16_t keep_on_screen(int16_t metric, int16_t pos, int16_t size)
{
  int16_t room = (int16_t)(uint16_t)(GetSystemMetrics(metric) - size);
  int16_t limit;
  if (room > pos) limit = pos;
  else limit = (int16_t)(uint16_t)(GetSystemMetrics(metric) - size);
  if (limit < 0) return 0;
  room = (int16_t)(uint16_t)(GetSystemMetrics(metric) - size);
  if (room > pos) return pos;
  return (int16_t)(uint16_t)(GetSystemMetrics(metric) - size);
}

/* Takes the size of a layout function's DX:AX result (width + 4, height + 4) plus 2 as the window size. */
static void take_layout_size(uint32_t result)
{
  SHOW_WIDTH = (int16_t)(uint16_t)((uint16_t)result + 2);
  SHOW_HEIGHT = (int16_t)(uint16_t)((uint16_t)(result >> 16) + 2);
}

/* show_info_popup: 24:0x091e (Ghidra FUN_10b8_091e), 1286 bytes with the code the inventory leaves out
 * (24:0x0968 to 0x0e23). ENTER 0x42,0; PUSH DI; PUSH SI. FAR CDECL (hwnd, x, y); AX is the result of
 * SetCapture. Opens the information pop-up for the descriptor DS:0x0BC0 at the client point (x, y):
 * converts the point to the screen, measures the pop-up (the text of the kind, with the layout
 * functions run in measuring mode: the width and height of the window), moves its corner so that
 * the window ends at the mouse and stays on the screen, creates the window of class "starspopup"
 * (a bordered, visible pop-up) with the 8-point font, stores it in DS:0x0bbe and captures the mouse.
 * POPUPWNDPROC closes it on the next mouse button release. For an unknown kind the window size is
 * whatever the stack holds. */
DECOMP(24, 0x091e, show_info_popup, FAR_CDECL)
uint16_t show_info_popup(uint16_t hwnd, uint16_t x, uint16_t y)
{
  FRAME_ENTER_REGS(SHOW_FRAME, 2);
  SHOW_POINT.x = (int16_t)x;
  SHOW_POINT.y = (int16_t)y;
  ClientToScreen(hwnd, bp_far(SHOW_POINT_AT));
  uint16_t hdc = GetDC(hwnd);
  uint16_t old_font = SelectObject(hdc, g_font_arial_8pt);
  switch (INFO_WORD(INFO_KIND)) {
  case INFO_KIND_PLANET_MINERAL: {
    uint16_t sample = string_text(STR_SAMPLE_MINERAL_LINE);
    SHOW_WIDTH = (int16_t)(uint16_t)(text_extent(hdc, ds_far(sample), ds_string_length(sample)) + 8);
    SHOW_HEIGHT = (int16_t)(uint16_t)(3 * line_height() + 8);
    if ((int16_t)INFO_WORD(INFO_ARG_14) >= 0) SHOW_HEIGHT = (int16_t)(uint16_t)(SHOW_HEIGHT + line_height());
    break;
  }
  case INFO_KIND_PLAYER: {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    uint16_t name = player_name(INFO_WORD(INFO_ID));
    SHOW_WIDTH = (int16_t)(uint16_t)(text_extent(hdc, ds_far(name), ds_string_length(name)) + 8);
    int16_t player_width = (int16_t)(uint16_t)(text_extent(hdc, ds_far(DS_STR_PLAYER_16), 10) + 8);
    if (player_width > SHOW_WIDTH) SHOW_WIDTH = player_width;
    SHOW_HEIGHT = (int16_t)(uint16_t)((line_height() + POPUP_MARGIN) * 2);
    break;
  }
  case INFO_KIND_FLEET:
    measure_fleet_list(hdc);
    break;
  case INFO_KIND_PLACE: {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    SHOW_HEIGHT = (int16_t)(uint16_t)((line_height() + 2) << 2);
    uint16_t label = string_text(STR_PLANET);
    SHOW_WIDTH = (int16_t)(uint16_t)(text_extent(hdc, ds_far(label), ds_string_length(label)) + 8);
    uint16_t name = planet_name(g_selected_planet);
    (void)SelectObject(hdc, g_font_arial_8pt);
    int16_t name_width = text_extent(hdc, ds_far(name), ds_string_length(name));
    uint16_t sample = string_text(STR_SAMPLE_ID);
    int16_t id_width = text_extent(hdc, ds_far(sample), 4);
    if (id_width < name_width) id_width = name_width;
    SHOW_WIDTH = (int16_t)(uint16_t)(SHOW_WIDTH + id_width);
    break;
  }
  case INFO_KIND_ENVIRONMENT:
    take_layout_size(call_seg24(OFF_LAYOUT_ENVIRONMENT, (const uint16_t[]){hdc, 0}, 2));
    break;
  case INFO_KIND_ZIP_ORDER_HELP:
    take_layout_size(call_seg24(OFF_LAYOUT_ZIP_HELP, (const uint16_t[]){hdc, 0, 0}, 3));
    break;
  case INFO_KIND_COLONISTS:
    take_layout_size(call_seg24(OFF_LAYOUT_COLONISTS, (const uint16_t[]){hdc, 0}, 2));
    break;
  case INFO_KIND_MINE_FACTORY:
    take_layout_size(call_seg24(OFF_LAYOUT_MINES, (const uint16_t[]){hdc, MEASURE_TEXT_WIDTH, 0}, 3));
    break;
  case INFO_KIND_TECH_ITEM:
    SHOW_WIDTH = (int16_t)(uint16_t)(((int16_t)line_height() > TECH_PANEL_SMALL_FONT_LIMIT ? TECH_PANEL_WIDE_EXTRA : 0) + TECH_PANEL_WIDTH);
    SHOW_HEIGHT = (int16_t)(uint16_t)(((uint16_t)(3 * line_height()) << 2) + g_cell_height_10pt + TECH_PANEL_HEIGHT_EXTRA);
    break;
  case INFO_KIND_TEXT:
    take_layout_size(call_seg24(OFF_LAYOUT_TOOLTIP, (const uint16_t[]){hdc, INFO_WORD(INFO_ID), 0}, 3));
    break;
  case INFO_KIND_DESIGN:
  case INFO_KIND_DESIGN_B: {
    NEAR_AT(uint16_t, DS_PANEL_DESIGN_FLAG_MAYBE) = INFO_WORD(INFO_KIND) == INFO_KIND_DESIGN ? 0 : 1;
    NEAR_AT(uint32_ua, DS_PANEL_DESIGN_PTR_MAYBE) = INFO_DWORD(INFO_ID);
    prepare_design_panel();
    SHOW_WIDTH = DESIGN_PANEL_WIDTH;
    SHOW_HEIGHT = (int16_t)(uint16_t)(7 * line_height() + DESIGN_PANEL_HEIGHT_EXTRA);
    if ((g_ui_flags_b_maybe & GAME_FLAG_BOTH_BUTTONS_MASK) != 0 && INFO_WORD(INFO_KIND) == INFO_KIND_DESIGN) {
      SHOW_HEIGHT = (int16_t)(uint16_t)(SHOW_HEIGHT + 3 * line_height());
    }
    break;
  }
  case INFO_KIND_RESOURCES:
    take_layout_size(call_seg24(OFF_LAYOUT_RESOURCES, (const uint16_t[]){hdc, MEASURE_TEXT_WIDTH, 0}, 3));
    break;
  case INFO_KIND_EMPTY:
    SHOW_WIDTH = EMPTY_PANEL_WIDTH;
    SHOW_HEIGHT = EMPTY_PANEL_HEIGHT;
    break;
  default:
    break;
  }
  (void)SelectObject(hdc, old_font);
  (void)ReleaseDC(hwnd, hdc);
  SHOW_POINT.x = (int16_t)(SHOW_POINT.x - SHOW_WIDTH);
  SHOW_POINT.y = (int16_t)(SHOW_POINT.y - SHOW_HEIGHT);
  SHOW_POINT.x = keep_on_screen(SM_CXSCREEN, SHOW_POINT.x, SHOW_WIDTH);
  SHOW_POINT.y = keep_on_screen(SM_CYSCREEN, SHOW_POINT.y, SHOW_HEIGHT);
  g_hwnd_popup_capture_maybe = CreateWindow(ds_far(DS_POPUP_CLASS), 0, WS_POPUP | WS_VISIBLE | WS_BORDER, SHOW_POINT.x, SHOW_POINT.y,
                                            SHOW_WIDTH, SHOW_HEIGHT, hwnd, 0, g_app_instance, 0);
  (void)SendMessage(g_hwnd_popup_capture_maybe, WM_SETFONT, g_font_arial_8pt, 0);
  uint16_t previous_capture = SetCapture(g_hwnd_popup_capture_maybe);
  FRAME_LEAVE();
  return previous_capture;
}
#undef SHOW_FRAME
#undef SHOW_POINT_AT
#undef SHOW_WIDTH_AT
#undef SHOW_HEIGHT_AT
#undef SHOW_NAME_BUF_AT
#undef SHOW_POINT
#undef SHOW_WIDTH
#undef SHOW_HEIGHT

/* ---- 24:0x0e24 popup_menu_choose ----------------------------------------------------------------- */

#define MENU_FRAME 0xac
#define MENU_POINT_AT (-0x16)  /* POINT16: where the menu opens, converted to the screen */
#define MENU_TEXT_AT (-0xa8)   /* the text of the item being added, with every '&' doubled */
#define MENU_MSG_AT (-0x28)    /* MSG16 of the PeekMessage after the menu closed */

/* The item text for AppendMenu: the string at DS:src, every '&' doubled (so that it is not taken as a
 * keyboard mnemonic), in the frame buffer. */
static void copy_menu_text(uint16_t src)
{
  uint8_t *dest = LIN(bp_far(MENU_TEXT_AT));
  const uint8_t *text = LINN(src);
  while (*text != 0) {
    uint8_t c = *text++;
    *dest++ = c;
    if (c == AMPERSAND) *dest++ = c;
  }
  *dest = 0;
}
static inline void append_separator(uint16_t menu) { (void)AppendMenu(menu, MF_SEPARATOR, 0, 0); }
static inline void append_text_item(uint16_t menu, uint16_t flags, uint16_t id)
{
  (void)AppendMenu(menu, flags, id, bp_far(MENU_TEXT_AT));
}

/* popup_menu_choose: 24:0x0e24 (Ghidra FUN_10b8_0e24), 838 bytes. ENTER 0xac,0; PUSH DI; PUSH SI. FAR CDECL
 * (hwnd, x, y, count, items, labels, selected, right_button); AX = the chosen index (also in DS:0x4c84),
 * 0xffff when nothing was chosen. Shows a pop-up menu at the client point (x, y) of the window and
 * returns which entry the player picked. The entries come from two lists:
 *  - items (a near pointer, may be 0): count dwords {id word, flags word}; the text is, by the flags,
 *    "Deep Space" (0x1000), the string of the string table with that id (0x4000), the name of the
 *    thing 7:0x1902 (0x2000) / 7:0x1a70 (0x8000) names, else the name of the planet with the id; a
 *    dword of 0xffffffff is a separator;
 *  - labels (a near pointer, may be 0): count words, each a near pointer to the text of the entry
 *    (0xff 0x00 = separator). A 0 word starts a sub-menu: the next word is its title and the
 *    following entries up to the next 0 word are its items (checked when the matching word of
 *    items is non-zero, or when it is the selected entry if items is 0).
 * With labels the entry texts replace those of items, except that selected == -2 with labels makes the
 * check marks come from the item dwords. selected is the index shown with a check mark. Ids of the
 * menu entries are 0x3a98 + index; the choice is read back with a PeekMessage of the WM_COMMAND
 * that TrackPopupMenu posted to the main window. right_button lets the right mouse button pick too. */
DECOMP(24, 0x0e24, popup_menu_choose, FAR_CDECL)
uint16_t popup_menu_choose(uint16_t hwnd, uint16_t x, uint16_t y, int16_t count, uint16_t items, uint16_t labels, int16_t selected,
                           uint16_t right_button)
{
  FRAME_ENTER_REGS(MENU_FRAME, 2);
  uint16_t submenu = 0;
  BP_LOCAL(POINT16, MENU_POINT_AT).x = (int16_t)x;
  BP_LOCAL(POINT16, MENU_POINT_AT).y = (int16_t)y;
  ClientToScreen(hwnd, bp_far(MENU_POINT_AT));
  uint16_t menu = CreatePopupMenu();
  g_popup_choice_maybe = 0xffff;
  uint16_t index = 0;
  while ((int16_t)index < count) {
    if (items == 0 || (selected == (int16_t)SELECTED_PER_ITEM && labels != 0)) {
      uint16_t entry = (uint16_t)(index + index + labels);
      if (NEAR_AT(uint16_ua, entry) == 0) {
        /* a sub-menu: its title, then the entries up to the next 0 word */
        uint16_t title = NEAR_AT(uint16_ua, entry + 2);
        uint16_t group_checked = items != 0 ? NEAR_AT(uint16_ua, (uint16_t)((uint16_t)(index << 2) + items + 4)) : 0;
        submenu = CreatePopupMenu();
        index = (uint16_t)(index + 2);
        uint16_t next = index;
        if ((int16_t)index < count) {
          uint16_t item_at = (uint16_t)((uint16_t)(index << 2) + items);
          uint16_t label_at = (uint16_t)(index + index + labels);
          while (NEAR_AT(uint16_ua, label_at) != 0) {
            uint16_t checked;
            if (items == 0) {
              checked = selected == (int16_t)next ? 1 : 0;
              group_checked = (uint16_t)(group_checked | checked);
            } else {
              checked = NEAR_AT(uint16_ua, item_at);
            }
            uint16_t label = NEAR_AT(uint16_ua, label_at);
            if (NEAR_AT(uint8_t, label) == MENU_LABEL_SEPARATOR && NEAR_AT(uint8_t, label + 1) == 0) {
              append_separator(submenu);
            } else {
              copy_menu_text(label);
              append_text_item(submenu, checked != 0 ? MF_CHECKED : 0, (uint16_t)(next + MENU_ID_BASE));
            }
            item_at = (uint16_t)(item_at + 4);
            label_at = (uint16_t)(label_at + 2);
            next++;
            if (!((int16_t)next < count)) break;
          }
        }
        (void)AppendMenu(menu, (group_checked != 0 ? MF_CHECKED : 0) | MF_POPUP, submenu, ds_far(title));
        index = next;
      } else {
        uint16_t label = NEAR_AT(uint16_ua, entry);
        if (NEAR_AT(uint8_t, label) == MENU_LABEL_SEPARATOR && NEAR_AT(uint8_t, NEAR_AT(uint16_ua, entry) + 1) == 0) {
          append_separator(menu);
        } else {
          copy_menu_text(label);
          uint32_t checked;
          if (selected == (int16_t)SELECTED_PER_ITEM) {
            uint16_t item = (uint16_t)(index << 2);
            checked = (uint32_t)NEAR_AT(uint16_ua, (uint16_t)(item + items)) | ((uint32_t)NEAR_AT(uint16_ua, (uint16_t)(item + items + 2)) << 16);
          } else {
            checked = selected == (int16_t)index ? 1 : 0;
          }
          append_text_item(menu, checked != 0 ? MF_CHECKED : 0, (uint16_t)(index + MENU_ID_BASE));
        }
      }
    } else {
      uint16_t item = (uint16_t)((uint16_t)(index << 2) + items);
      if (NEAR_AT(uint32_ua, item) == ITEM_SEPARATOR) {
        append_separator(menu);
      } else {
        uint16_t flags = NEAR_AT(uint16_ua, item + 2);
        uint16_t text;
        if ((flags & ITEM_DEEP_SPACE) != 0) {
          text = DS_STR_DEEP_SPACE;
        } else if ((flags & ITEM_STRING_ID) != 0) {
          text = string_text(NEAR_AT(uint16_ua, item));
        } else if ((flags & ITEM_FLEET_MAYBE) != 0) {
          text = menu_fleet_name_maybe(NEAR_AT(uint16_ua, item));
        } else if ((flags & ITEM_TARGET_MAYBE) != 0) {
          text = menu_target_name_maybe((uint16_t)(NEAR_AT(uint16_ua, item) | ITEM_TARGET_MAYBE));
        } else {
          text = planet_name(NEAR_AT(uint16_ua, item));
        }
        copy_menu_text(text);
        append_text_item(menu, selected == (int16_t)index ? MF_CHECKED : 0, (uint16_t)(index + MENU_ID_BASE));
      }
    }
    index++;
  }
  (void)TrackPopupMenu(menu, right_button != 0 ? TPM_RIGHTBUTTON : 0, BP_LOCAL(POINT16, MENU_POINT_AT).x,
                       BP_LOCAL(POINT16, MENU_POINT_AT).y, 0, g_main_window, 0);
  (void)DestroyMenu(menu);
  if (submenu != 0) (void)DestroyMenu(submenu);
  uint16_t got = PeekMessage(bp_far(MENU_MSG_AT), g_main_window, WM_COMMAND, WM_COMMAND, PM_NOYIELD);
  uint16_t command = BP_LOCAL(MSG16, MENU_MSG_AT).wParam;
  if (got != 0 && command >= MENU_ID_BASE && command < MENU_ID_LIMIT) g_popup_choice_maybe = (uint16_t)(command - MENU_ID_BASE);
  FRAME_LEAVE();
  return g_popup_choice_maybe;
}
#undef MENU_FRAME
#undef MENU_POINT_AT
#undef MENU_TEXT_AT
#undef MENU_MSG_AT

/* ---- 24:0x116a layout_environment_info ----------------------------------------------------------- */

#define ENV_FRAME 0x72
#define ENV_X_AT (-0x10)   /* the text cursor: x and y, which 8:0x90b0 / 8:0x90fe advance through near pointers */
#define ENV_Y_AT (-0x12)
#define ENV_TEXT_AT (-0x70) /* "This value is N% away from the ideal value ...", formatted on the stack */
#define ENV_X BP_LOCAL(int16_t, ENV_X_AT)
#define ENV_Y BP_LOCAL(int16_t, ENV_Y_AT)
/* The name of the environment value (Gravity, Temperature or Radiation) the descriptor names in INFO_ARG_4. */
#define ENV_AXIS_NAME NEAR_AT(uint16_t, (uint16_t)(DS_ENV_NAME_PTRS + 2 * INFO_WORD(INFO_ARG_4)))

/* Addresses the planet environment byte of the axis INFO_ARG_4 of the planet record at off:seg. */
static inline uint8_t *planet_environment_byte(uint16_t seg, uint16_t off)
{
  return FAR_REC(uint8_t, seg, (uint16_t)(off + INFO_WORD(INFO_ARG_4) + offsetof(struct Planet, env_current)));
}

/* layout_environment_info: 24:0x116a (Ghidra FUN_10b8_116a), 1849 bytes. ENTER 0x72,0; PUSH DI; PUSH SI. FAR CDECL
 * (hdc, draw); AX = width + 4, DX = height + 4 of the text. The pop-up of one environment value of a
 * planet (descriptor kind 5): INFO_ARG_4 = the axis (0 gravity, 1 temperature, 2 radiation), INFO_ID
 * the planet id, INFO_ARG_6 the planet's current value (or negative when unknown), INFO_ARG_8 and
 * INFO_ARG_A the lowest and highest value the player's technology can terraform it to (negative: no
 * technology), INFO_ARG_C the value of the race's ideal, INFO_ARG_E and INFO_ARG_10 the lowest and
 * highest value the race can live with (0xffff: immune to this value).
 * It writes: "<axis> currently is <value>."; whether the colonists are immune or prefer planets
 * where the value lies between two numbers; what the player's terraforming technology can modify
 * ("within the range of ... to ..."); and, if terraforming to the best reachable value would raise the
 * planet's habitability (9:0x474c with the byte of the planet record changed for a moment), "If
 * you were to terraform <axis> to <value>, the planet's value would improve to <habitability>%.";
 * last how far the current value lies outside the race's range or away from its ideal. */
DECOMP(24, 0x116a, layout_environment_info, FAR_CDECL)
uint32_t layout_environment_info(uint16_t hdc, uint16_t draw)
{
  FRAME_ENTER_REGS(ENV_FRAME, 2);
#define ADVANCE(str, len) draw_text_advance(hdc, bp_near(ENV_X_AT), ENV_Y, (str), (len), draw)
#define NEW_LINE() (ENV_X = POPUP_MARGIN, ENV_Y = (int16_t)(uint16_t)(ENV_Y + line_height()))
  ENV_Y = POPUP_MARGIN;
  uint16_t widest = POPUP_MARGIN;
  ENV_X = POPUP_MARGIN;
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  (void)ADVANCE(ENV_AXIS_NAME, 0);
  (void)SelectObject(hdc, g_font_arial_8pt);
  (void)ADVANCE(string_text(STR_CURRENTLY_IS), 0);
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  if ((int16_t)INFO_WORD(INFO_ARG_6) >= 0) {
    (void)ADVANCE(environment_text(INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ARG_6)), 0);
  } else {
    (void)ADVANCE(string_text(STR_UNKNOWN), 0);
  }
  (void)SelectObject(hdc, g_font_arial_8pt);
  (void)ADVANCE(DS_STR_PERIOD_A, 1);
  if (ENV_X > POPUP_MARGIN) widest = (uint16_t)ENV_X;
  ENV_X = POPUP_MARGIN;
  ENV_Y = (int16_t)(uint16_t)(ENV_Y + line_height());
  if (INFO_WORD(INFO_ARG_E) == ENV_NO_LIMIT) {
    /* the race is immune to this value */
    (void)ADVANCE(string_text(STR_IMMUNE_TO), 0);
    if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
    NEW_LINE();
    (void)ADVANCE(string_text(STR_TO_THE_EFFECTS_OF), 0);
    (void)ADVANCE(ENV_AXIS_NAME, 0);
    (void)ADVANCE(DS_STR_PERIOD_B, 1);
  } else {
    (void)ADVANCE(string_text(STR_COLONISTS_PREFER), 0);
    if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
    ENV_X = POPUP_MARGIN;
    ENV_Y = (int16_t)(uint16_t)(ENV_Y + line_height());
    (void)ADVANCE(ENV_AXIS_NAME, 0);
    (void)ADVANCE(string_text(STR_IS_BETWEEN), 0);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(environment_text(INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ARG_E)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(string_text(STR_AND), 5);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(environment_text(INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ARG_10)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(DS_STR_PERIOD_C, 1);
  }
  if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
  NEW_LINE();
  if ((int16_t)INFO_WORD(INFO_ARG_8) > -1) {
    /* what the technology of the player can do to the value */
    (void)ADVANCE(string_text(STR_YOU_POSSESS_TECHNOLOGY), 0);
    if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
    NEW_LINE();
    (void)ADVANCE(string_text(STR_MODIFY_THE), 0);
    (void)ADVANCE(ENV_AXIS_NAME, 0);
    (void)ADVANCE(string_text(STR_ON), 4);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
    NEW_LINE();
    (void)ADVANCE(string_text(STR_WITHIN_THE_RANGE_OF), 0);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(environment_text(INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ARG_8)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(string_text(STR_TO), 4);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(environment_text(INFO_WORD(INFO_ARG_4), INFO_WORD(INFO_ARG_A)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(DS_STR_PERIOD_D, 1);
    if ((int16_t)widest < ENV_X) widest = (uint16_t)ENV_X;
    ENV_Y = (int16_t)(uint16_t)(ENV_Y + line_height());
  }
  ENV_X = POPUP_MARGIN;
  if ((int16_t)INFO_WORD(INFO_ARG_8) > -1 && INFO_WORD(INFO_ARG_E) != ENV_NO_LIMIT && INFO_WORD(INFO_ARG_6) != INFO_WORD(INFO_ARG_C)) {
    /* the value terraforming would reach: the nearest end of the reachable range that lies on the
     * better side of the ideal (0xffff: nothing improves) */
    uint16_t best = ENV_NO_LIMIT;
    int16_t current = (int16_t)INFO_WORD(INFO_ARG_6), ideal = (int16_t)INFO_WORD(INFO_ARG_C);
    if (current < ideal) {
      int16_t reach_max = (int16_t)INFO_WORD(INFO_ARG_A);
      if (current < reach_max) best = (uint16_t)(reach_max <= ideal ? reach_max : ideal);
    } else {
      int16_t reach_min = (int16_t)INFO_WORD(INFO_ARG_8);
      if (current > reach_min) best = (uint16_t)(reach_min >= ideal ? reach_min : ideal);
    }
    if (best != ENV_NO_LIMIT) {
      uint32_t planet = planet_record(INFO_WORD(INFO_ID));
      uint16_t planet_off = (uint16_t)planet, planet_seg = (uint16_t)(planet >> 16);
      int16_t before = planet_habitability(planet_off, planet_seg, g_current_player);
      int8_t saved = (int8_t)*planet_environment_byte(planet_seg, planet_off);
      *planet_environment_byte(planet_seg, planet_off) = (uint8_t)best;
      int16_t after = planet_habitability(planet_off, planet_seg, g_current_player);
      *planet_environment_byte(planet_seg, planet_off) = (uint8_t)saved;
      if (after > before) {
        uint16_t wrap_width = (uint16_t)(widest - POPUP_MARGIN);
        uint16_t len = copy_string_text(STR_IF_YOU_TERRAFORM, DS_TEXT_BUFFER_OFF);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), DS_TEXT_BUFFER_OFF, len, POPUP_MARGIN, wrap_width, 0, 0, draw);
        (void)SelectObject(hdc, g_font_arial_bold_8pt);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), ENV_AXIS_NAME, 0, POPUP_MARGIN, wrap_width, 0, 0, draw);
        (void)SelectObject(hdc, g_font_arial_8pt);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), string_text(STR_TO), 4, POPUP_MARGIN, wrap_width, 0, 0, draw);
        (void)SelectObject(hdc, g_font_arial_bold_8pt);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), environment_text(INFO_WORD(INFO_ARG_4), best), 0, POPUP_MARGIN, wrap_width,
                          0, 0, draw);
        (void)SelectObject(hdc, g_font_arial_8pt);
        len = copy_string_text(STR_VALUE_WOULD_IMPROVE, DS_TEXT_BUFFER_OFF);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), DS_TEXT_BUFFER_OFF, len, POPUP_MARGIN, wrap_width, 0, 0, draw);
        (void)SelectObject(hdc, g_font_arial_bold_8pt);
        len = format_ds1(DS_FMT_PERCENT_PERIOD, (uint16_t)after);
        draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), DS_TEXT_BUFFER_OFF, len, POPUP_MARGIN, wrap_width, 0, 0, draw);
      }
    }
  }
  if (INFO_WORD(INFO_ARG_E) != ENV_NO_LIMIT && INFO_WORD(INFO_ARG_6) != INFO_WORD(INFO_ARG_C) && INFO_WORD(INFO_ARG_6) != ENV_NO_LIMIT) {
    /* how far the current value lies outside the range of the race, or from its ideal inside it */
    int16_t current = (int16_t)INFO_WORD(INFO_ARG_6);
    int16_t away;
    if (current < (int16_t)INFO_WORD(INFO_ARG_E)) {
      away = (int16_t)(uint16_t)(INFO_WORD(INFO_ARG_E) - INFO_WORD(INFO_ARG_6));
    } else if (current > (int16_t)INFO_WORD(INFO_ARG_10)) {
      away = (int16_t)(uint16_t)(INFO_WORD(INFO_ARG_6) - INFO_WORD(INFO_ARG_10));
    } else {
      int16_t distance = (int16_t)(uint16_t)(INFO_WORD(INFO_ARG_6) - INFO_WORD(INFO_ARG_C));
      away = (int16_t)(uint16_t)(-(distance < 0 ? (int16_t)(uint16_t)-distance : distance));
    }
    uint16_t format_id;
    if (away < 0) {
      away = (int16_t)(uint16_t)-away;
      format_id = STR_FMT_AWAY_FROM_IDEAL;
    } else {
      format_id = STR_FMT_OUTSIDE_RANGE;
    }
    (void)copy_string_text(format_id, DS_TEXT_BUFFER_OFF);
    uint16_t len = format_text(bp_far(ENV_TEXT_AT), ds_far(DS_TEXT_BUFFER_OFF), 1, (const uint16_t[]){(uint16_t)away});
    (void)SelectObject(hdc, g_font_arial_8pt);
    draw_text_wrapped(hdc, bp_near(ENV_X_AT), bp_near(ENV_Y_AT), bp_near(ENV_TEXT_AT), len, POPUP_MARGIN, (uint16_t)(widest - POPUP_MARGIN), 0, 0,
                      draw);
  }
  if (ENV_X > POPUP_MARGIN) ENV_Y = (int16_t)(uint16_t)(ENV_Y + line_height());
  uint32_t size = layout_result(widest, (uint16_t)ENV_Y);
  FRAME_LEAVE();
  return size;
#undef ADVANCE
#undef NEW_LINE
}
#undef ENV_FRAME
#undef ENV_X_AT
#undef ENV_Y_AT
#undef ENV_TEXT_AT
#undef ENV_X
#undef ENV_Y
#undef ENV_AXIS_NAME

/* ---- 24:0x18a4 layout_colonist_info -------------------------------------------------------------- */

#define COLONIST_FRAME 0xe0
#define COLONIST_X_AT (-0x0c)      /* the text cursor, advanced by the text helpers through near pointers */
#define COLONIST_Y_AT (-0x0e)
#define COLONIST_PLANET_AT (-0x4a) /* a copy of the planet record, filled by 7:0x0358 and edited by 7:0x46fc */
#define COLONIST_TEXT_AT (-0xe0)   /* the sentence about the population growth, formatted on the stack */
#define COLONIST_X BP_LOCAL(int16_t, COLONIST_X_AT)
#define COLONIST_Y BP_LOCAL(int16_t, COLONIST_Y_AT)
#define COLONIST_PLANET BP_LOCAL(struct Planet, COLONIST_PLANET_AT)
/* The colonists on the planet (the fourth amount of the record). */
#define COLONISTS_ON_PLANET COLONIST_PLANET.surface_amounts_maybe[3]
enum {
  PLANET_INFO_LEVEL_POPULATION = 3, /* from this information level on the record shows the population */
  PLANET_DEFENSE_SHIFT = 12,        /* the top nibble of the population word: the planetary defense level */
  PLANET_POPULATION_MASK = 0x0fff,  /* the population estimate of the record, in hundreds */
  DEFENSE_COVERAGE_PER_LEVEL = 6,   /* the shown coverage is 6 percent per level + 3 */
  DEFENSE_COVERAGE_BASE = 3
};

/* layout_colonist_info: 24:0x18a4 (Ghidra FUN_10b8_18a4), 2024 bytes. ENTER 0xe0,0; PUSH DI; PUSH SI. FAR CDECL
 * (hdc, draw); AX = width + 4, DX = height + 4. The pop-up of the colonists of a planet (descriptor kind
 * 7, INFO_ID = the planet id). It works on a copy of the planet record (BP-0x4a, filled by
 * 7:0x0358) and writes by the owner of the planet:
 *  - the player's own planet: "Your population on <planet> is <colonists>.";
 *  - another player's planet: "The enemy population on <planet> is approximately N." when the
 *    information level of the record is 3 or more, else "is unknown.";
 *  - nobody's: "<planet> is uninhabited."
 * Then, from the information level 3 on, what colonists would find there (habitability 9:0x474c of
 * the record for the player, the population it supports 9:0x48cc): a negative habitability "<planet> will
 * kill off approximately x.y% of your colonists each turn", a positive one "<planet> will support a
 * population of up to N of your colonists" (the player's own planet) or "If you were to colonize <planet>, it
 * would support up to N of your colonists"; for the player's own habitable planet whose capacity
 * exceeds the colonists, the growth of the next year (7:0x46fc) "Your population on ... will grow by
 * N to M next year." / "will not grow next year."; for another player's colonized planet its
 * planetary defense coverage (the top nibble of the population word, 6 * nibble + 3 percent,
 * or none). The name returned by 7:0x1cce is the shared text buffer DS:0x5866, which the "no
 * planetary defenses" line is copied to as well, so the line is drawn from the pointer of the name. */
DECOMP(24, 0x18a4, layout_colonist_info, FAR_CDECL)
uint32_t layout_colonist_info(uint16_t hdc, uint16_t draw)
{
  FRAME_ENTER_REGS(COLONIST_FRAME, 2);
#define ADVANCE(str, len) draw_text_advance(hdc, bp_near(COLONIST_X_AT), COLONIST_Y, (str), (len), draw)
#define WRAP(str, len) \
  draw_text_wrapped(hdc, bp_near(COLONIST_X_AT), bp_near(COLONIST_Y_AT), (str), (len), POPUP_MARGIN, widest, 0, 0, draw)
#define NEW_LINE() (COLONIST_X = POPUP_MARGIN, COLONIST_Y = (int16_t)(uint16_t)(COLONIST_Y + line_height()))
  uint16_t widest = POPUP_MARGIN;
  COLONIST_X = POPUP_MARGIN;
  COLONIST_Y = POPUP_MARGIN;
  planet_view_copy(INFO_WORD(INFO_ID), bp_near(COLONIST_PLANET_AT));
  (void)SelectObject(hdc, g_font_arial_8pt);
  if (COLONIST_PLANET.owner == g_current_player) {
    (void)ADVANCE(string_text(STR_YOUR_POPULATION_ON), 0);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(string_text(STR_IS), widest); /* the length 4 of " is " is the width still held in widest */
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)format_ds_long(NEAR_AT(uint16_t, DS_FMT_PTR_HUNDREDS), COLONISTS_ON_PLANET);
    if (COLONISTS_ON_PLANET == 0) NEAR_AT(uint8_t, DS_TEXT_BUFFER_OFF + 1) = 0;
    (void)ADVANCE(DS_TEXT_BUFFER_OFF, 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(DS_STR_PERIOD_E, 1);
  } else if (COLONIST_PLANET.owner != PLAYER_OWNER_NONE) {
    (void)ADVANCE(string_text(STR_ENEMY_POPULATION_ON), 0);
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    if (COLONIST_PLANET.info_level >= PLANET_INFO_LEVEL_POPULATION) {
      (void)ADVANCE(string_text(STR_IS_APPROXIMATELY), 0);
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      (void)format_ds1(DS_FMT_HUNDREDS, (uint16_t)((COLONIST_PLANET.population_estimate_maybe & PLANET_POPULATION_MASK) << 2));
      if ((COLONIST_PLANET.population_estimate_maybe & PLANET_POPULATION_MASK) == 0) NEAR_AT(uint8_t, DS_TEXT_BUFFER_OFF + 1) = 0;
      (void)ADVANCE(DS_TEXT_BUFFER_OFF, 0);
      (void)SelectObject(hdc, g_font_arial_8pt);
    } else {
      (void)ADVANCE(string_text(STR_IS_UNKNOWN), 0);
    }
    (void)ADVANCE(DS_STR_PERIOD_F, 1);
  } else {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(string_text(STR_IS_UNINHABITED), 0);
  }
  if (COLONIST_X > (int16_t)widest) widest = (uint16_t)COLONIST_X;
  NEW_LINE();
  int16_t habitability = 0;
  if (COLONIST_PLANET.info_level >= PLANET_INFO_LEVEL_POPULATION) {
    uint32_t capacity = planet_capacity(INFO_WORD(INFO_ID), g_current_player);
    habitability = planet_habitability(bp_near(COLONIST_PLANET_AT), rt_ss, g_current_player);
    if (habitability < 0) {
      /* the colonists would die: x.y percent of them each turn */
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
      (void)SelectObject(hdc, g_font_arial_8pt);
      (void)ADVANCE(string_text(STR_WILL_KILL_OFF), 0);
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      uint16_t deaths = format_ds2(g_fmt_ptr_decimal_pair_maybe, (uint16_t)(habitability / -10), (uint16_t)((int16_t)(uint16_t)-habitability % 10));
      (void)ADVANCE(DS_TEXT_BUFFER_OFF, deaths);
      (void)SelectObject(hdc, g_font_arial_8pt);
      (void)ADVANCE(string_text(STR_OF), 3); /* 3 = the length of " of" */
      if (COLONIST_X > (int16_t)widest) widest = (uint16_t)COLONIST_X;
      NEW_LINE();
      if (COLONIST_PLANET.owner == g_current_player) (void)ADVANCE(string_text(STR_YOUR_COLONISTS_EACH_TURN), 0);
      else (void)ADVANCE(string_text(STR_OF_ALL_COLONISTS), 0);
    } else if ((int32_t)capacity > 0) {
      /* the planet supports colonists: say how many (on the own planet or if the player colonized it) */
      if (COLONIST_PLANET.owner == g_current_player) {
        (void)SelectObject(hdc, g_font_arial_bold_8pt);
        (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
        (void)SelectObject(hdc, g_font_arial_8pt);
        (void)ADVANCE(string_text(STR_WILL_SUPPORT_UP_TO), 0);
      } else {
        (void)SelectObject(hdc, g_font_arial_8pt);
        (void)ADVANCE(string_text(STR_IF_YOU_COLONIZE), 0);
        (void)SelectObject(hdc, g_font_arial_bold_8pt);
        (void)ADVANCE(planet_name(INFO_WORD(INFO_ID)), 0);
        (void)SelectObject(hdc, g_font_arial_8pt);
        (void)ADVANCE(string_text(STR_IT_WOULD), 0);
      }
      uint16_t held_widest = widest;
      if (COLONIST_X > (int16_t)held_widest) held_widest = (uint16_t)COLONIST_X;
      NEW_LINE();
      if (COLONIST_PLANET.owner != g_current_player) (void)ADVANCE(string_text(STR_SUPPORT_UP_TO), 0);
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      uint16_t shown = format_ds_long(NEAR_AT(uint16_t, DS_FMT_PTR_HUNDREDS), capacity);
      (void)ADVANCE(DS_TEXT_BUFFER_OFF, shown);
      (void)SelectObject(hdc, g_font_arial_8pt);
      (void)ADVANCE(string_text(STR_OF_YOUR_COLONISTS), 0);
      widest = held_widest;
    } else {
      COLONIST_Y = (int16_t)(uint16_t)(COLONIST_Y - line_height());
    }
    if (COLONIST_X > (int16_t)widest) widest = (uint16_t)COLONIST_X;
    NEW_LINE();
    if (COLONIST_PLANET.owner == g_current_player && habitability >= 0 && (int32_t)capacity > (int32_t)COLONISTS_ON_PLANET) {
      /* the player's own habitable planet below its capacity: the growth of the next year */
      uint16_t len = copy_string_text(STR_YOUR_POPULATION_ON_LONG, bp_near(COLONIST_TEXT_AT));
      WRAP(bp_near(COLONIST_TEXT_AT), len);
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      WRAP(planet_name(COLONIST_PLANET.id), 0);
      (void)SelectObject(hdc, g_font_arial_8pt);
      uint32_t growth = planet_growth(bp_near(COLONIST_PLANET_AT), rt_ss);
      if (habitability != 0 && (int32_t)growth > 0) {
        uint16_t format = string_text(STR_FMT_WILL_GROW);
        uint32_t grown = growth + COLONISTS_ON_PLANET;
        len = format_text(bp_far(COLONIST_TEXT_AT), ds_far(format), 4,
                          (const uint16_t[]){(uint16_t)growth, (uint16_t)(growth >> 16), (uint16_t)grown, (uint16_t)(grown >> 16)});
      } else {
        len = copy_string_text(STR_WILL_NOT_GROW, bp_near(COLONIST_TEXT_AT));
      }
      WRAP(bp_near(COLONIST_TEXT_AT), len);
      COLONIST_X = POPUP_MARGIN;
      COLONIST_Y = (int16_t)(uint16_t)(COLONIST_Y + line_height());
    } else if (COLONIST_PLANET.owner != g_current_player && COLONIST_PLANET.owner != PLAYER_OWNER_NONE) {
      /* another player's planet: how well it is defended */
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      uint16_t name = planet_name(COLONIST_PLANET.id);
      WRAP(name, 0);
      (void)SelectObject(hdc, g_font_arial_8pt);
      uint16_t text, len;
      if ((COLONIST_PLANET.population_estimate_maybe >> PLANET_DEFENSE_SHIFT) == 0) {
        len = copy_string_text(STR_NO_DEFENSES, DS_TEXT_BUFFER_OFF);
        text = name;
      } else {
        uint16_t format = string_text(STR_FMT_DEFENSES);
        uint16_t coverage = (uint16_t)((COLONIST_PLANET.population_estimate_maybe >> PLANET_DEFENSE_SHIFT) * DEFENSE_COVERAGE_PER_LEVEL +
                                       DEFENSE_COVERAGE_BASE);
        len = format_text(bp_far(COLONIST_TEXT_AT), ds_far(format), 1, (const uint16_t[]){coverage});
        text = bp_near(COLONIST_TEXT_AT);
      }
      WRAP(text, len);
      COLONIST_X = POPUP_MARGIN;
      COLONIST_Y = (int16_t)(uint16_t)(COLONIST_Y + line_height());
    }
  }
  uint32_t size = layout_result(widest, (uint16_t)COLONIST_Y);
  FRAME_LEAVE();
  return size;
#undef ADVANCE
#undef WRAP
#undef NEW_LINE
}
#undef COLONIST_FRAME
#undef COLONIST_X_AT
#undef COLONIST_Y_AT
#undef COLONIST_PLANET_AT
#undef COLONIST_TEXT_AT
#undef COLONIST_X
#undef COLONIST_Y
#undef COLONIST_PLANET
#undef COLONISTS_ON_PLANET

/* ---- 24:0x208c layout_zip_order_help ------------------------------------------------------------- */

#define ZIP_FRAME 0x0a
#define ZIP_X_AT (-6)   /* the text cursor x, advanced by 8:0x90b0 through a near pointer */
#define ZIP_X BP_LOCAL(int16_t, ZIP_X_AT)

/* layout_zip_order_help: 24:0x208c (Ghidra FUN_10b8_208c), 813 bytes. ENTER 0xa,0; PUSH DI; PUSH SI. FAR CDECL
 * (hdc, center_x, draw); AX = width + 4, DX = height + 4. The help text of the ZipOrd quick transport
 * orders (descriptor kind 6): the title "ZipOrd " centred on center_x (only when drawing), four lines
 * that explain what a ZipOrd is, then the four orders QuikLoad, QuikDrop, WaitLoad and Clear, each a
 * bold name followed by a line of explanation. */
DECOMP(24, 0x208c, layout_zip_order_help, FAR_CDECL)
uint32_t layout_zip_order_help(uint16_t hdc, uint16_t center_x, uint16_t draw)
{
  FRAME_ENTER_REGS(ZIP_FRAME, 2);
#define ADVANCE(str) draw_text_advance(hdc, bp_near(ZIP_X_AT), (int16_t)y, (str), 0, draw)
  int16_t widest = POPUP_MARGIN;
  ZIP_X = POPUP_MARGIN;
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  if (draw != 0) {
    uint16_t title = string_text(STR_ZIPORD);
    draw_text_centered(hdc, (int16_t)center_x, POPUP_MARGIN, title, ds_string_length(title));
  }
  uint16_t y = (uint16_t)(g_text_line_height + POPUP_MARGIN);
  (void)SelectObject(hdc, g_font_arial_8pt);
  /* the four lines of the explanation */
  static const uint16_t help_lines[] = {STR_ZIP_HELP_LINE_1, STR_ZIP_HELP_LINE_2, STR_ZIP_HELP_LINE_3, STR_ZIP_HELP_LINE_4};
  for (uint16_t line = 0; line < 4; line++) {
    (void)ADVANCE(string_text(help_lines[line]));
    if (ZIP_X > widest) widest = ZIP_X;
    ZIP_X = POPUP_MARGIN;
    y = (uint16_t)(y + g_text_line_height);
  }
  y = (uint16_t)(y + g_text_line_height);
  /* the four orders: the bold name, its explanation, then one and a half lines down */
  static const uint16_t order_names[] = {STR_QUICK_LOAD, STR_QUICK_DROP, STR_WAIT_LOAD, STR_CLEAR};
  static const uint16_t order_helps[] = {STR_QUICK_LOAD_HELP, STR_QUICK_DROP_HELP, STR_WAIT_LOAD_HELP, STR_CLEAR_HELP};
  for (uint16_t order = 0; order < 4; order++) {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    (void)ADVANCE(string_text(order_names[order]));
    (void)SelectObject(hdc, g_font_arial_8pt);
    (void)ADVANCE(string_text(order_helps[order]));
    if (widest < ZIP_X) widest = ZIP_X;
    ZIP_X = POPUP_MARGIN;
    if (order < 3) y = (uint16_t)(y + (uint16_t)((int16_t)g_text_line_height >> 1) + g_text_line_height);
  }
  uint32_t size = layout_result((uint16_t)widest, (uint16_t)(y + g_text_line_height));
  FRAME_LEAVE();
  return size;
#undef ADVANCE
}
#undef ZIP_FRAME
#undef ZIP_X_AT
#undef ZIP_X

/* ---- 24:0x23ba layout_mine_factory_info ---------------------------------------------------------- */

#define MINES_FRAME 0x16
#define MINES_X_AT (-0x12)    /* the text cursor x and y and the widest x, advanced by the text helpers */
#define MINES_MAX_X_AT (-0x10)
#define MINES_Y_AT (-0x0e)
#define MINES_X BP_LOCAL(int16_t, MINES_X_AT)
#define MINES_MAX_X BP_LOCAL(int16_t, MINES_MAX_X_AT)
#define MINES_Y BP_LOCAL(int16_t, MINES_Y_AT)

/* layout_mine_factory_info: 24:0x23ba (Ghidra FUN_10b8_23ba), 488 bytes with the code the inventory leaves out
 * (24:0x24c3 to 0x25a1: the jump table and the cases). ENTER 0x16,0; PUSH DI; PUSH SI. FAR CDECL (hdc, width, draw);
 * AX = widest x + 4, DX = y + one line + 2. The pop-up of the mines or factories of a planet
 * (descriptor kind 8): INFO_ARG_A = 0 for mines, else factories; INFO_ID the planet. Writes the title
 * "<Mine|Factory> Info" centred (when drawing) and then, for a race whose attribute PLAYER_ATTR_RACE is 8
 * (RACE_VALUE_NO_FACTORIES), the sentence that it is incapable of building them; else nine pieces of
 * wrapped text: lines of the string table (the explanation texts from string 0xc9 on) alternating with
 * the planet name, the counts (INFO_ARG_6 and INFO_ARG_4, each followed by the noun in the singular
 * for 1, else the plural) and the number INFO_ARG_8. */
DECOMP(24, 0x23ba, layout_mine_factory_info, FAR_CDECL)
uint32_t layout_mine_factory_info(uint16_t hdc, uint16_t width, uint16_t draw)
{
  FRAME_ENTER_REGS(MINES_FRAME, 2);
  uint16_t line_id = STR_MINE_FACTORY_FIRST;
  uint16_t text_width = (uint16_t)(width - 8);
  MINES_X = POPUP_MARGIN;
  MINES_MAX_X = POPUP_MARGIN;
  MINES_Y = 2;
  uint16_t singular, plural;
  if (INFO_WORD(INFO_ARG_A) != 0) {
    singular = DS_STR_FACTORY;
    plural = DS_STR_FACTORIES;
  } else {
    singular = DS_STR_MINE;
    plural = DS_STR_MINES;
  }
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  if (draw != 0) {
    uint16_t title_len = format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(DS_FMT_NAME_INFO), 2, (const uint16_t[]){singular, MEM_SEL_DGROUP});
    draw_text_centered(hdc, (int16_t)((int16_t)text_width >> 1), MINES_Y, DS_TEXT_BUFFER_OFF, title_len);
  }
  MINES_Y = (int16_t)(uint16_t)(MINES_Y + g_text_line_height + POPUP_MARGIN);
  if (player_attribute(current_player_row(), PLAYER_ATTR_RACE) == RACE_VALUE_NO_FACTORIES) {
    (void)SelectObject(hdc, g_font_arial_8pt);
    uint16_t message = string_text((uint16_t)(STR_NO_FACTORIES + (INFO_WORD(INFO_ARG_A) == 0 ? 1 : 0)));
    draw_text_wrapped(hdc, bp_near(MINES_X_AT), bp_near(MINES_Y_AT), message, 0, POPUP_MARGIN, text_width, bp_near(MINES_MAX_X_AT), 0, draw);
  } else {
    for (uint16_t piece = 0; piece <= 8; piece++) {
      (void)SelectObject(hdc, g_font_arial_bold_8pt);
      uint16_t text;
      switch (piece) {
      case 1:
      case 5: {
        /* the count of the mines / factories, with the noun: "1 Mine", "7 Mines" */
        uint16_t count = piece == 1 ? INFO_WORD(INFO_ARG_6) : INFO_WORD(INFO_ARG_4);
        uint16_t noun = (int16_t)count == 1 ? singular : plural;
        format_text(ds_far(DS_TEXT_BUFFER_OFF), ds_far(DS_FMT_COUNT_NOUN), 3, (const uint16_t[]){count, noun, MEM_SEL_DGROUP});
        text = DS_TEXT_BUFFER_OFF;
        break;
      }
      case 3:
        text = planet_name(INFO_WORD(INFO_ID));
        break;
      case 7:
        (void)format_ds1(g_fmt_ptr_a_maybe, INFO_WORD(INFO_ARG_8));
        text = DS_TEXT_BUFFER_OFF;
        break;
      default:
        (void)SelectObject(hdc, g_font_arial_8pt);
        text = string_text(line_id);
        line_id++;
        break;
      }
      draw_text_wrapped(hdc, bp_near(MINES_X_AT), bp_near(MINES_Y_AT), text, 0, POPUP_MARGIN, text_width, bp_near(MINES_MAX_X_AT), 0, draw);
    }
  }
  uint32_t size = layout_result_below((uint16_t)MINES_MAX_X, (uint16_t)MINES_Y);
  FRAME_LEAVE();
  return size;
}
#undef MINES_FRAME
#undef MINES_X_AT
#undef MINES_MAX_X_AT
#undef MINES_Y_AT
#undef MINES_X
#undef MINES_MAX_X
#undef MINES_Y

/* ---- 24:0x25a2 layout_resource_info -------------------------------------------------------------- */

#define RESOURCE_FRAME 0x14
#define RESOURCE_X_AT (-0x10)  /* the text cursor x and y and the widest x, advanced by the text helpers */
#define RESOURCE_MAX_X_AT (-0x0e)
#define RESOURCE_Y_AT (-0x0c)
#define RESOURCE_X BP_LOCAL(int16_t, RESOURCE_X_AT)
#define RESOURCE_MAX_X BP_LOCAL(int16_t, RESOURCE_MAX_X_AT)
#define RESOURCE_Y BP_LOCAL(int16_t, RESOURCE_Y_AT)

/* layout_resource_info: 24:0x25a2 (Ghidra FUN_10b8_25a2), 406 bytes with the code the inventory leaves out
 * (24:0x2660 to 0x2737: the jump table and the cases). ENTER 0x14,0; PUSH DI; PUSH SI. FAR CDECL (hdc, width, draw);
 * AX = widest x + 4, DX = y + one line + 2. The pop-up of the resources of a planet (descriptor
 * kind 12): the title "Resource Info" centred (when drawing), then 9 pieces of wrapped text (10 for
 * a race with PLAYER_ATTR_RACE == 8): bold or plain, alternating the lines of the string table (from
 * string 0xe9 on) with the planet name (INFO_ID), the numbers INFO_ARG_4, INFO_ARG_4 - INFO_ARG_6
 * (or "None") and INFO_ARG_6; with INFO_ARG_4 == INFO_ARG_6 the text ends after the sixth piece. */
DECOMP(24, 0x25a2, layout_resource_info, FAR_CDECL)
uint32_t layout_resource_info(uint16_t hdc, uint16_t width, uint16_t draw)
{
  FRAME_ENTER_REGS(RESOURCE_FRAME, 2);
  uint16_t last_piece = (uint16_t)((player_attribute(current_player_row(), PLAYER_ATTR_RACE) == RACE_VALUE_NO_FACTORIES ? 1 : 0) + 8);
  uint16_t line_id = STR_RESOURCE_FIRST;
  uint16_t text_width = (uint16_t)(width - 8);
  RESOURCE_X = POPUP_MARGIN;
  RESOURCE_MAX_X = POPUP_MARGIN;
  RESOURCE_Y = 2;
  (void)SelectObject(hdc, g_font_arial_bold_8pt);
  if (draw != 0) {
    uint16_t title = string_text(STR_RESOURCE_INFO);
    draw_text_centered(hdc, (int16_t)((int16_t)text_width >> 1), RESOURCE_Y, title, ds_string_length(title));
  }
  RESOURCE_Y = (int16_t)(uint16_t)(RESOURCE_Y + g_text_line_height + POPUP_MARGIN);
  for (uint16_t piece = 0; (int16_t)piece <= (int16_t)last_piece; piece++) {
    (void)SelectObject(hdc, g_font_arial_bold_8pt);
    if (piece == 6 && INFO_WORD(INFO_ARG_4) == INFO_WORD(INFO_ARG_6)) break; /* nothing left to say: the text ends here */
    uint16_t text;
    switch (piece) {
    case 0:
      text = planet_name(INFO_WORD(INFO_ID));
      break;
    case 2:
      (void)format_ds1(g_fmt_ptr_a_maybe, INFO_WORD(INFO_ARG_4));
      text = DS_TEXT_BUFFER_OFF;
      break;
    case 4: {
      int16_t remaining = (int16_t)(uint16_t)(INFO_WORD(INFO_ARG_4) - INFO_WORD(INFO_ARG_6));
      if (remaining > 0) {
        (void)format_ds1(g_fmt_ptr_a_maybe, (uint16_t)remaining);
        text = DS_TEXT_BUFFER_OFF;
      } else {
        text = string_text(STR_NONE);
      }
      break;
    }
    case 7:
      (void)format_ds1(g_fmt_ptr_a_maybe, INFO_WORD(INFO_ARG_6));
      text = DS_TEXT_BUFFER_OFF;
      break;
    default:
      /* a line of the string table in the plain font */
      (void)SelectObject(hdc, g_font_arial_8pt);
      text = string_text(line_id);
      line_id++;
      break;
    }
    draw_text_wrapped(hdc, bp_near(RESOURCE_X_AT), bp_near(RESOURCE_Y_AT), text, 0, POPUP_MARGIN, text_width, bp_near(RESOURCE_MAX_X_AT), 0, draw);
  }
  uint32_t size = layout_result_below((uint16_t)RESOURCE_MAX_X, (uint16_t)RESOURCE_Y);
  FRAME_LEAVE();
  return size;
}
#undef RESOURCE_FRAME
#undef RESOURCE_X_AT
#undef RESOURCE_MAX_X_AT
#undef RESOURCE_Y_AT
#undef RESOURCE_X
#undef RESOURCE_MAX_X
#undef RESOURCE_Y

/* ---- 24:0x2738 layout_text_tooltip --------------------------------------------------------------- */

#define TOOLTIP_FRAME 0x0a
#define TOOLTIP_X_AT (-6)   /* the text cursor x and y and the widest x, advanced by the text helper */
#define TOOLTIP_Y_AT (-4)
#define TOOLTIP_MAX_X_AT (-2)
#define TOOLTIP_X BP_LOCAL(int16_t, TOOLTIP_X_AT)
#define TOOLTIP_Y BP_LOCAL(int16_t, TOOLTIP_Y_AT)
#define TOOLTIP_MAX_X BP_LOCAL(int16_t, TOOLTIP_MAX_X_AT)

/* layout_text_tooltip: 24:0x2738 (Ghidra FUN_10b8_2738), 106 bytes. ENTER 0xa,0; PUSH SI. FAR CDECL (hdc, width, draw);
 * AX = widest x + 4, DX = y + one line + 2. The pop-up of a plain text (descriptor kind 10): wraps the
 * string whose near pointer is INFO_ARG_4 in the 8-point font to width - 8 pixels. */
DECOMP(24, 0x2738, layout_text_tooltip, FAR_CDECL)
uint32_t layout_text_tooltip(uint16_t hdc, uint16_t width, uint16_t draw)
{
  FRAME_ENTER_REGS(TOOLTIP_FRAME, 1);
  TOOLTIP_MAX_X = POPUP_MARGIN;
  TOOLTIP_X = POPUP_MARGIN;
  TOOLTIP_Y = 2;
  (void)SelectObject(hdc, g_font_arial_8pt);
  draw_text_wrapped(hdc, bp_near(TOOLTIP_X_AT), bp_near(TOOLTIP_Y_AT), INFO_WORD(INFO_ARG_4), 0, POPUP_MARGIN, (uint16_t)(width - 8),
                    bp_near(TOOLTIP_MAX_X_AT), 0, draw);
  uint32_t size = layout_result_below((uint16_t)TOOLTIP_MAX_X, (uint16_t)TOOLTIP_Y);
  FRAME_LEAVE();
  return size;
}
#undef TOOLTIP_FRAME
#undef TOOLTIP_X_AT
#undef TOOLTIP_Y_AT
#undef TOOLTIP_MAX_X_AT
#undef TOOLTIP_X
#undef TOOLTIP_Y
#undef TOOLTIP_MAX_X
