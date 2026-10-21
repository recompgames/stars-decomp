/* Diff tests for decompiled segment 24 (src/seg24.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 24 is the information pop-up of the main window and the pop-up menu:
 *   24:0x0000  POPUPWNDPROC: the window procedure of the pop-up (paint, erase, close on a button release)
 *   24:0x00ca  paint the pop-up for the descriptor DS:0x0bc0 (kinds 1 to 14)
 *   24:0x091e  measure the pop-up for the descriptor and open its window next to the mouse
 *   24:0x0e24  the pop-up menu: build it from a list, track it, return the choice
 *   24:0x116a  layout of the environment-value text of a planet (gravity, temperature, radiation)
 *   24:0x18a4  layout of the colonist text of a planet
 *   24:0x208c  layout of the help text of the ZipOrd quick transport orders
 *   24:0x23ba  layout of the mines / factories text of a planet
 *   24:0x25a2  layout of the resource text of a planet
 *   24:0x2738  layout of a plain tooltip text
 * Every scenario builds its state by writing DGROUP and a few far blocks directly; DIFF then
 * snapshots it and compares the original with the C version (return value, all memory outside the
 * callee's own frame, the ordered API call log, which here also holds the test doubles' calls).
 *
 * Test doubles (docs/CONVENTIONS.md rule j2) are logged thunks, installed over the first 5 bytes of the
 * callee in the code segment image, so the same bytes serve the ORIGINAL run and the C version's
 * CALL_ORIG; the functions under test are never doubled. Three remain, 25:0x185e, 25:0x2ed6 and 31:0x61b0, each
 * with its own reason at g_stubs. A double returns 0 and hashes, at every call, the whole data segment below
 * the stack, the arguments and (25:0x185e, 25:0x2ed6) the 8-byte rectangle it is handed, so a store made before
 * or after the call it should follow (or precede) shows as a difference in the hash log, which DIFF compares as
 * arena memory. Everything else runs as ORIGINAL code in both runs: the text helpers of segment 8 (8:0x8df4,
 * 0x8dc0, 0x9072, 0x90b0, 0x90fe, 0x9398 and what they call), the names 7:0x1cce, 7:0x1902, 7:0x1a70 (over a table
 * of map objects and a sorted table of fleets the scenarios build, with 7:0x01a0, 7:0x14a8, 7:0x27e4), 7:0x01e4,
 * 7:0x0b84, 7:0x3b9e, 9:0x3e72, the planet copy 7:0x0358, the habitability 9:0x474c, the capacity 9:0x48cc and the
 * growth 7:0x46fc (over planet records and player rows the scenarios fill: the environment of the race per axis
 * at row + 0x10, 0x13, 0x16, the environment of the planet at planet + 0xc, strictly ordered so that no division
 * by zero happens), 25:0x450c (over design records whose hull word is below 0x40 and whose part count is at
 * most 16, which 2:0x507c reads) and the player attributes 28:0x21ee, 28:0x41d4. main() turns
 * rt_set_mode(RT_MODE_DECOMP) on, so the decompiled functions of this file call each other, and 27:0x1e06
 * (src/seg27.c), as C through rt_call_far in the C run, while the ORIGINAL run executes the original code.
 *
 * The scenarios of 24:0x0e24 feed the keys of TrackPopupMenu through the GetMessage hook (user_wait_hook) from a script
 * kept in the arena. The windows are not visible and the test class answers WM_NCCREATE with 1 and every other message
 * with 0: a visible window would be sent WM_PAINT again and again by the menu loop, since the class never paints.
 * The strings and menu lists that DGROUP offsets must name (near pointers) are placed at DS:0x6800 and up, the far end of
 * the stack area (the data segment has no other free space; the stack in use is far above it): they are inputs, no function writes them.
 *
 * SEG24_ONLY=<tags> runs only the scenarios whose tag appears in the string (2738 208c 25a2 23ba 116a
 * 18a4 00ca 091e 0e24 0000). */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/menu.h"
#include "runtime/user.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }
static void ds_w32(uint16_t off, uint32_t v) { memcpy(LINN(off), &v, 4); }

/* A deterministic generator for the scenarios (xorshift32). */
static uint32_t g_rng = 1;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}
static uint32_t rn(uint32_t n) { return rnd() % n; }
static bool chance(uint32_t num, uint32_t den) { return rn(den) < num; }

/* A zeroed, locked global block; returns its far pointer (offset 0). */
static uint32_t gblock(uint32_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  uint32_t p = GlobalLock(h);
  CHECK(p != 0);
  return p;
}
static uint32_t fstr(const char *str) {
  uint32_t p = gblock((uint32_t)strlen(str) + 1);
  memcpy(LIN(p), str, strlen(str) + 1);
  return p;
}

/* Windows. The class procedure answers WM_NCCREATE (0x81) with 1 and every other message with 0:
 * PUSH BP; MOV BP,SP; CMP WORD [BP+0xC],0x81; JNE +5; MOV AX,1; JMP +2; XOR AX,AX; XOR DX,DX; POP BP; RETF 10.
 * (A procedure that answers 1 to WM_ENTERIDLE keeps the menu tracker in its idle loop.) */
static void register_test_class(const char *name) {
  static const uint8_t code[] = {0x55, 0x8B, 0xEC, 0x81, 0x7E, 0x0C, 0x81, 0x00, 0x75, 0x05, 0xB8, 0x01, 0x00,
                                 0xEB, 0x02, 0x31, 0xC0, 0x31, 0xD2, 0x5D, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = fstr(name);
  w->hInstance = 0x1234;
  CHECK(RegisterClass(wc) != 0);
}
static uint16_t mkwin(uint32_t style, int x, int y, int cx, int cy, uint16_t parent, uint16_t id) {
  uint16_t h = CreateWindow(fstr("SEG24TEST"), fstr("w"), style, (int16_t)x, (int16_t)y, (int16_t)cx, (int16_t)cy, parent, id, 0x1234, 0);
  CHECK(h != 0);
  return h;
}

/* ---- the test doubles ---- */

#define STUB_BLKSZ 0x8000
#define STUB_LOG0 0x4000
static uint32_t g_stub_blk;

typedef struct StubCfg {
  const char *name;
  uint16_t seg, off;
  uint8_t nargs; /* argument words */
  int8_t ssw;    /* argument word index of a near pointer (SS) whose ssn bytes are hashed, -1 none */
  uint8_t ssn;
} StubCfg;

enum { S_25_185E, S_25_2ED6, S_31_61B0, S_NSTUB };
static const StubCfg g_stubs[S_NSTUB] = {
    /* Rule j2, one reason per double. Each double returns 0 (AX and DX) and logs its call.
     * 25:0x185e (1773 bytes) draws the picture of a ship design. It needs the picture bitmap of the hull: 10:0x2d28
     * copies it from the hull sprite sheet (a bitmap resource of the game) and 8:0xaa4e stretches a picture with
     * GlobalLock and StretchDIBits, and the game's palette (SelectPalette, RealizePalette) for the hull colours. This
     * harness loads none of the game's bitmap resources and has no palette (test_seg27.c doubles 10:0x2d28 and
     * 8:0xaa4e for the same reason). Blind spot: what it draws.
     * 25:0x2ed6 (2793 bytes) draws the details of a ship design (its parts, mass, cost, armour). It totals the 16 slots
     * of the design through 30:0x216e, 30:0x2636, 29:0x0f74 and 9:0x55f4 over the part tables of the tech tree (hull,
     * engine and weapon tables in segments 29 and 30, filled from the game files) and the player's tech levels: the
     * scenario has none of those tables. Blind spot: what it draws.
     * 31:0x61b0 refreshes the main window: it reads and writes the turn-view state DS:0x53f0 to DS:0x5400, calls the
     * view layout 31:0x663a, 31:0x62ea, 31:0x9c58 and 31:0x5cc2, and uses SetWindowText, GetWindowRect and
     * InvalidateRect on the main window DS:0x2806 as a frame window with a title bar and client area laid out by the
     * game's start-up (4:0x0000, 1:0x0000); the scenario has only an empty test window there. Blind spot: the
     * refresh.
     * Each double hashes at every call the data segment below the stack and its argument words (and for 25:0x185e
     * and 25:0x2ed6 the 8-byte rectangle they are handed), so a store moved across the call, or the order of the
     * calls against the API calls, is checked. */
    [S_25_185E] = {"25:185e", 25, 0x185e, 4, 2, 8},
    [S_25_2ED6] = {"25:2ed6", 25, 0x2ed6, 4, 3, 8},
    [S_31_61B0] = {"31:61b0", 31, 0x61b0, 0, -1, 0},
};

static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static void stub_handler(HCall *c) {
  const StubCfg *cfg = &g_stubs[c->ordinal];
  uint8_t *blk = LIN(g_stub_blk);
  uint32_t h = 2166136261u;
  h = fnv(h, LINN(0), harness_stack_limit());
  for (unsigned i = 0; i < cfg->nargs; i++) {
    uint16_t w = hcall_word(c, i);
    h = fnv(h, (const uint8_t *)&w, 2);
  }
  if (cfg->ssw >= 0 && cfg->ssn) {
    uint16_t p = hcall_word(c, (unsigned)cfg->ssw);
    h = fnv(h, LIN(MK_FP(c->regs->ss, p)), cfg->ssn);
  }
  uint16_t at;
  memcpy(&at, blk, 2);
  if (at + 4 > STUB_BLKSZ) at = STUB_LOG0;
  memcpy(blk + at, &h, 4);
  at = (uint16_t)(at + 4);
  memcpy(blk, &at, 2);
  c->ret = 0;
}
static void stubs_clear_all(void) {
  uint16_t at = STUB_LOG0;
  memcpy(LIN(g_stub_blk), &at, 2);
}
static void stubs_init(void) {
  g_stub_blk = gblock(STUB_BLKSZ);
  CHECK_EQ(LO(g_stub_blk), 0);
  for (unsigned i = 0; i < S_NSTUB; i++) {
    const StubCfg *cfg = &g_stubs[i];
    uint32_t th = harness_add_import("STUBS", (uint16_t)i, cfg->name, HK_VARARGS, (uint16_t)(2 * cfg->nargs), HR_RET32, stub_handler);
    CHECK(th != 0);
    uint8_t c[5] = {0xEA, (uint8_t)th, (uint8_t)(th >> 8), (uint8_t)(th >> 16), (uint8_t)(th >> 24)};
    memcpy(LIN(MK_FP(SEG_SEL(cfg->seg), cfg->off)), c, sizeof c);
  }
  stubs_clear_all();
}

/* ---- the world: handles, metrics, players, planets, designs, fleets ---- */

#define PLAYER_ROW(p) ((uint16_t)(0x5a64 + 0xc0 * (p)))
#define SCRATCH 0x6800 /* DS:0x6800 and up lies in the stack area, far below the stack in use: strings, records (inputs only) */
#define INFO(off) DS_W((uint16_t)(0x0bc0 + (off)))
#define INFO32(off) (*(uint32_ua *)LINN((uint16_t)(0x0bc0 + (off))))

static uint16_t g_dc;      /* a screen DC */
static uint32_t g_planets; /* the planet table */
static uint32_t g_dsg;     /* a ship design table (16 designs of 0x8d bytes) */
#define NPLAN 12

/* The fonts, the brush and the text metrics the pop-up code uses: two different fonts, so that the
 * widths of the bold and the plain text differ. */
static void world_gdi(void) {
  DS_W(0x0010) = GetStockObject(0);
  static const uint16_t fonts[] = {10, 11, 12, 13, 14, 16}; /* OEM_FIXED, ANSI_FIXED, ANSI_VAR, SYSTEM, DEVICE_DEFAULT, SYSTEM_FIXED */
  DS_W(0x292e) = GetStockObject(fonts[rn(6)]);
  DS_W(0x2930) = GetStockObject(fonts[rn(6)]);
  DS_W(0x267a) = (uint16_t)(chance(1, 6) ? 1 + rn(30) : 10 + rn(10));
  DS_W(0x54ee) = (uint16_t)(chance(1, 6) ? rn(40) : 14 + rn(4));
  DS_W(0x54f4) = 0x1234;
}

/* All 16 player rows random, with plausible names, and the race attribute (the signed byte at +0x4c)
 * 8 for the players where attr8 is set. */
static void world_rows(bool race8) {
  for (unsigned p = 0; p < 16; p++) {
    uint8_t *row = LINN(PLAYER_ROW(p));
    for (unsigned i = 0; i < 0xc0; i++) row[i] = (uint8_t)rnd();
    if (chance(1, 3)) snprintf((char *)row + 0x80, 0x20, "Al"); /* a name shorter than "Player #16" */
    else if (chance(1, 5)) snprintf((char *)row + 0x80, 0x20, "R&D %u", p); /* '&' in a name: the pop-up menu doubles it */
    else snprintf((char *)row + 0x80, 0x20, "Empire %u", p);
    if (chance(1, 3)) snprintf((char *)row + 0xa0, 0x20, "Als");
    else snprintf((char *)row + 0xa0, 0x20, "Empires %u", p);
    row[0x4c] = (uint8_t)(race8 ? 8 : (chance(1, 3) ? 8 : rn(8)));
    /* The environment the race lives in, per axis (gravity, temperature, radiation): the lowest value at +0x13, the ideal at
     * +0x10 and the highest at +0x16 (0xff: the race is immune to the axis). 9:0x474c divides by the distances between them. */
    for (unsigned a = 0; a < 3; a++) {
      unsigned lo = rn(50), ideal = lo + 1 + rn(20), hi = ideal + 1 + rn(30);
      row[0x13 + a] = (uint8_t)lo;
      row[0x10 + a] = (uint8_t)ideal;
      row[0x16 + a] = (uint8_t)(chance(1, 6) ? 0xff : hi);
    }
    uint16_t fleets = (uint16_t)(rnd() & 0xf000); /* the fleet count in the low 12 bits stays 0: 7:0x14a8 sums it */
    memcpy(row + 4, &fleets, 2);
  }
}

/* The planet table DS:0xb6 (n planets, ids 0 to n - 1 in order) and the table of the planet name strings DS:0x29e8. */
static void world_planets(unsigned n) {
  if (!g_planets) g_planets = gblock(0x38 * NPLAN + 0x40);
  DS_W(0x4d96) = (uint16_t)n;
  DS_W(0x007a) = (uint16_t)n;
  ds_w32(0x00b6, g_planets);
  for (unsigned k = 0; k < NPLAN; k++) {
    uint8_t *pl = LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * k)));
    for (unsigned i = 0; i < 0x38; i++) pl[i] = (uint8_t)rnd();
    uint16_t id = (uint16_t)k;
    memcpy(pl, &id, 2);
    for (unsigned a = 0; a < 3; a++) pl[0xc + a] = (uint8_t)rn(101); /* the planet's gravity, temperature, radiation */
    DS_W((uint16_t)(0x29e8 + 2 * k)) = (uint16_t)(1 + rn(0x3e0));
  }
}

/* The design tables of the players (DS:0xbe + 4 * player), all pointing to one table with names. */
static void world_designs(void) {
  if (!g_dsg) g_dsg = gblock(0x8d * 16 + 0x20);
  for (unsigned i = 0; i < 0x8d * 16; i++) ((uint8_t *)LIN(g_dsg))[i] = (uint8_t)rnd();
  for (unsigned d = 0; d < 16; d++) {
    uint8_t *rec = (uint8_t *)LIN(g_dsg) + 0x8d * d;
    snprintf((char *)rec + 8, 0x20, "Design %c%u", 'A' + (int)rn(26), d);
    rec[0] = (uint8_t)rn(0x40);
    rec[1] = 0;
  }
  for (unsigned p = 0; p < 16; p++) {
    ds_w32((uint16_t)(0xbe + 4 * p), g_dsg);
    ds_w32((uint16_t)(0x10c + 4 * p), g_dsg); /* the starbase designs: 9:0x48cc reads one for a race with attribute 14 equal to 8 */
  }
}

/* A text of random words for the tooltips, at DS:`at`. */
static void put_text(uint16_t at, unsigned words, bool long_words) {
  static const char *const w[] = {"the", "planet", "has", "a", "very", "long", "habitability", "colonists", "Stars!", "x", "of", "fleet",
                                  "supercalifragilisticexpialidocious", "mineral", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
  char buf[400];
  buf[0] = 0;
  for (unsigned i = 0; i < words; i++) {
    size_t len = strlen(buf);
    if (len > 300) break;
    snprintf(buf + len, sizeof buf - len, "%s%s", i ? " " : "", w[rn(long_words ? 15 : 11)]);
  }
  memcpy(LINN(at), buf, strlen(buf) + 1);
}

/* ---- 24:0x2738: a plain tooltip text ---- */

static void t_2738(void) {
  for (unsigned it = 0; it < 220; it++) {
    g_rng = 0x27380001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    put_text(SCRATCH, it % 25, it % 3 == 0);
    INFO(0) = 10;
    INFO(4) = SCRATCH;
    uint16_t width = (uint16_t)(chance(1, 8) ? rn(20) : 0x30 + rn(0x180));
    DIFF(24, 0x2738, FAR_CDECL, g_dc, width, it & 1);
  }
  /* boundary: no text, width 0, width 8 (a line width of 0), 0xffff, the pointer to an empty string */
  put_text(SCRATCH, 0, false);
  INFO(4) = SCRATCH;
  static const uint16_t widths[] = {0, 8, 9, 0x7fff, 0x8000, 0xffff, 0x100};
  for (unsigned i = 0; i < sizeof widths / 2; i++)
    for (uint16_t draw = 0; draw < 2; draw++) DIFF(24, 0x2738, FAR_CDECL, g_dc, widths[i], draw);
}

/* ---- 24:0x208c: the ZipOrd help text ---- */

static void t_208c(void) {
  for (unsigned it = 0; it < 120; it++) {
    g_rng = 0x208c0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    uint16_t center = (uint16_t)(chance(1, 6) ? rn(10) : 40 + rn(300));
    DIFF(24, 0x208c, FAR_CDECL, g_dc, center, it & 1);
  }
  /* boundary: centre 0 and 0xffff, the smallest and largest line heights */
  static const uint16_t line_heights[] = {0, 1, 2, 30, 0x7fff, 0xffff};
  for (unsigned i = 0; i < sizeof line_heights / 2; i++) {
    stubs_clear_all();
    world_gdi();
    DS_W(0x267a) = line_heights[i];
    for (uint16_t draw = 0; draw < 2; draw++) {
      DIFF(24, 0x208c, FAR_CDECL, g_dc, 0, draw);
      DIFF(24, 0x208c, FAR_CDECL, g_dc, 0xffff, draw);
    }
  }
}

/* ---- 24:0x25a2: the resources of a planet ---- */

static void t_25a2(void) {
  for (unsigned it = 0; it < 260; it++) {
    g_rng = 0x25a20001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    DS_W(0x014c) = (uint16_t)rn(16);
    INFO(0) = 12;
    INFO(2) = (uint16_t)rn(NPLAN);
    uint16_t a = (uint16_t)(chance(1, 4) ? 0 : chance(1, 4) ? 0xffff : rn(2000));
    INFO(4) = a;
    INFO(6) = (uint16_t)(chance(1, 4) ? a : chance(1, 4) ? (uint16_t)(a + rn(5)) : rn(2000)); /* equal, a little above, or random */
    uint16_t width = (uint16_t)(chance(1, 8) ? rn(20) : 0x30 + rn(0x180));
    DIFF(24, 0x25a2, FAR_CDECL, g_dc, width, it & 1);
  }
  /* the exits of the loop: equal numbers (the text ends at the seventh piece), a negative difference, width 0 and 8 */
  world_gdi();
  world_rows(true);
  INFO(0) = 12;
  INFO(2) = 3;
  static const uint16_t pairs[][2] = {{5, 5}, {0, 0}, {5, 6}, {0x8000, 0}, {0, 0x8000}, {0xffff, 0xffff}, {1, 0}};
  static const uint16_t widths[] = {0, 8, 9, 0xffff, 0x100};
  for (unsigned i = 0; i < sizeof pairs / 4; i++) {
    for (unsigned w = 0; w < sizeof widths / 2; w++) {
      INFO(4) = pairs[i][0];
      INFO(6) = pairs[i][1];
      DIFF(24, 0x25a2, FAR_CDECL, g_dc, widths[w], (w & 1));
    }
  }
}

/* ---- 24:0x23ba: the mines or factories of a planet ---- */

static void t_23ba(void) {
  for (unsigned it = 0; it < 260; it++) {
    g_rng = 0x23ba0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    DS_W(0x014c) = (uint16_t)rn(16);
    INFO(0) = 8;
    INFO(2) = (uint16_t)rn(NPLAN);
    INFO(4) = (uint16_t)(chance(1, 4) ? 1 : rn(300));
    INFO(6) = (uint16_t)(chance(1, 4) ? 1 : rn(300));
    INFO(8) = (uint16_t)rn(100);
    INFO(10) = (uint16_t)(chance(1, 2) ? 0 : 1 + rn(3)); /* 0 mines, else factories */
    uint16_t width = (uint16_t)(chance(1, 8) ? rn(20) : 0x30 + rn(0x180));
    DIFF(24, 0x23ba, FAR_CDECL, g_dc, width, it & 1);
  }
  /* boundary: counts 0, 1, 2 and 0xffff; the race that cannot build them (mines and factories); width 0 and 8 */
  world_gdi();
  world_rows(true);
  INFO(0) = 8;
  INFO(2) = 2;
  static const uint16_t counts[] = {0, 1, 2, 0xffff};
  for (unsigned i = 0; i < 4; i++) {
    INFO(4) = counts[i];
    INFO(6) = counts[3 - i];
    for (uint16_t type = 0; type < 2; type++) {
      INFO(10) = type;
      DIFF(24, 0x23ba, FAR_CDECL, g_dc, 0xc8, 1);
      DIFF(24, 0x23ba, FAR_CDECL, g_dc, 0, 0);
      DIFF(24, 0x23ba, FAR_CDECL, g_dc, 8, 1);
    }
  }
  DS_W(0x014c) = 3;
  ((uint8_t *)LINN(PLAYER_ROW(3)))[0x4c] = 8;
  for (uint16_t type = 0; type < 2; type++) {
    INFO(10) = type;
    DIFF(24, 0x23ba, FAR_CDECL, g_dc, 0xc8, 1); /* the race that cannot build them */
    DIFF(24, 0x23ba, FAR_CDECL, g_dc, 0xc8, 0);
  }
}

/* ---- 24:0x116a: an environment value of a planet ---- */

/* The descriptor of kind 5: axis, the planet, its value, the reachable range, the ideal and the race's range. */
static void info_environment(uint16_t axis, uint16_t planet, uint16_t current, uint16_t reach_min, uint16_t reach_max, uint16_t ideal,
                             uint16_t race_min, uint16_t race_max) {
  INFO(0) = 5;
  INFO(2) = planet;
  INFO(4) = axis;
  INFO(6) = current;
  INFO(8) = reach_min;
  INFO(10) = reach_max;
  INFO(12) = ideal;
  INFO(14) = race_min;
  INFO(16) = race_max;
}
static uint16_t value_or_none(uint32_t none_one_in) { return (uint16_t)(chance(1, none_one_in) ? 0xffff : rn(101)); }
static void t_116a(void) {
  for (unsigned it = 0; it < 700; it++) {
    g_rng = 0x116a0001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    DS_W(0x014c) = (uint16_t)rn(16);
    uint16_t current = value_or_none(8);
    uint16_t reach_min = chance(1, 3) ? 0xffff : (uint16_t)rn(101);
    uint16_t reach_max = chance(1, 3) ? 0xffff : (uint16_t)rn(101);
    uint16_t ideal = (uint16_t)rn(101);
    uint16_t race_min = chance(1, 5) ? 0xffff : (uint16_t)rn(60);
    uint16_t race_max = (uint16_t)(race_min == 0xffff ? rn(101) : race_min + rn(60));
    if (chance(1, 5)) current = ideal;
    info_environment((uint16_t)rn(3), (uint16_t)rn(NPLAN), current, reach_min, reach_max, ideal, race_min, race_max);
    DIFF(24, 0x116a, FAR_CDECL, g_dc, it & 1);
  }
  /* each branch of the best-value choice, with and without improvement, immune race, unknown value, boundary values */
  struct { uint16_t cur, rmin, rmax, ideal, lo, hi; } cases[] = {
      {10, 20, 40, 50, 30, 70}, {10, 5, 40, 50, 30, 70}, {60, 20, 40, 50, 30, 70}, {60, 55, 90, 50, 30, 70}, {60, 20, 40, 50, 0xffff, 0},
      {0, 0, 0, 0, 0, 0}, {100, 0, 100, 0, 0, 100}, {0xffff, 1, 2, 3, 4, 5}, {50, 0xffff, 0xffff, 50, 40, 60}, {50, 49, 51, 50, 40, 60},
      {0x8000, 0, 100, 50, 40, 60}, {0x7fff, 0, 100, 50, 40, 60}, {20, 0, 100, 50, 40, 60}, {80, 0, 100, 50, 40, 60}, {35, 10, 90, 50, 40, 60},
      {45, 10, 90, 50, 40, 60}, {55, 10, 90, 50, 40, 60}, {65, 10, 90, 50, 40, 60}};
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    for (uint16_t axis = 0; axis < 3; axis++) {
      for (unsigned rep = 0; rep < 4; rep++) { /* four planets and races each: the habitability changes with the byte the code sets */
        g_rng = 0x116a1000u + i * 64 + axis * 8 + rep;
        stubs_clear_all();
        world_gdi();
        world_rows(false);
        world_planets(NPLAN);
        DS_W(0x014c) = 2;
        info_environment(axis, (uint16_t)(5 + rep), cases[i].cur, cases[i].rmin, cases[i].rmax, cases[i].ideal, cases[i].lo, cases[i].hi);
        DIFF(24, 0x116a, FAR_CDECL, g_dc, 1);
        DIFF(24, 0x116a, FAR_CDECL, g_dc, 0);
      }
    }
  }
}

/* ---- 24:0x18a4: the colonists of a planet ---- */

/* The fields of planet `id` in the planet table that 7:0x0358 copies for the pop-up: owner (0xffff = nobody), information
 * level, population word, colonists (the dword at +0x28), and flag bit 1 (a starbase) for the race that builds none. */
static void set_planet(uint16_t id, uint16_t owner, uint8_t level, uint16_t population, uint32_t colonists) {
  uint8_t *pl = LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * id)));
  memcpy(pl + 2, &owner, 2);
  pl[4] = level;
  memcpy(pl + 0x12, &population, 2);
  memcpy(pl + 0x28, &colonists, 4);
}
static uint32_t random_colonists(void) {
  static const uint32_t fixed[] = {0, 1, 99, 100, 500, 1000, 4999, 5000, 9999, 20000, 100000};
  return chance(1, 3) ? rn(200000) : fixed[rn(11)];
}

static void t_18a4(void) {
  for (unsigned it = 0; it < 1500; it++) {
    g_rng = 0x18a40001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    uint16_t me = (uint16_t)rn(16);
    DS_W(0x014c) = me;
    uint16_t id = (uint16_t)rn(NPLAN);
    INFO(0) = 7;
    INFO(2) = id;
    unsigned who = rn(5);
    uint16_t owner = who < 2 ? me : who < 4 ? (uint16_t)((me + 1 + rn(15)) & 15) : 0xffff;
    uint8_t level = (uint8_t)(chance(1, 4) ? rn(3) : 3 + rn(4));
    uint16_t population = (uint16_t)(chance(1, 4) ? (rn(16) << 12) : chance(1, 3) ? rn(16) << 12 | rn(4000) : rnd());
    set_planet(id, owner, level, population, random_colonists());
    DIFF(24, 0x18a4, FAR_CDECL, g_dc, it & 1);
  }
  /* boundary: the three owners, the information levels 2 and 3, colonists 0, equal to and above the capacity, draw 0 and 1,
   * a race with attribute 14 equal to 8 (the capacity comes from a starbase design), several planets and races each */
  static const uint32_t colonists[] = {0, 500, 100000};
  for (unsigned mine = 0; mine < 3; mine++) {
    for (unsigned level = 2; level <= 3; level++) {
      for (unsigned c = 0; c < 3; c++) {
        for (unsigned race8 = 0; race8 < 2; race8++) {
          for (unsigned rep = 0; rep < 6; rep++) {
            g_rng = 0x18a41000u + mine * 997 + level * 331 + c * 61 + race8 * 13 + rep;
            stubs_clear_all();
            world_gdi();
            world_rows(race8 != 0);
            world_planets(NPLAN);
            DS_W(0x014c) = 4;
            INFO(0) = 7;
            INFO(2) = (uint16_t)(3 + rep);
            set_planet((uint16_t)(3 + rep), mine == 0 ? 4 : mine == 1 ? 7 : 0xffff, (uint8_t)level, (uint16_t)(rep << 12 | 0x123), colonists[c]);
            ((uint8_t *)LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * (3 + rep)))))[5] |= 2; /* a starbase, for the race of attribute 8 */
            DIFF(24, 0x18a4, FAR_CDECL, g_dc, 1);
            DIFF(24, 0x18a4, FAR_CDECL, g_dc, 0);
          }
        }
      }
    }
  }
}

/* colonists just below, equal to and just above the capacity 9:0x48cc gives for the planet: the comparison that decides
 * whether the next year's growth is shown. The capacity comes from the original code run once before the DIFF. */
static void t_18a4_capacity_edge(void) {
  for (unsigned rep = 0; rep < 60; rep++) {
    g_rng = 0x18a42000u + rep * 7919;
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    uint16_t me = (uint16_t)rn(16), id = (uint16_t)rn(NPLAN);
    DS_W(0x014c) = me;
    INFO(0) = 7;
    INFO(2) = id;
    set_planet(id, me, 3, (uint16_t)rnd(), 1000);
    /* the planet's environment inside the player's range (between the lowest and highest value, or the ideal): a habitable planet */
    uint8_t *pl = LIN(MK_FP(HI(g_planets), (uint16_t)(0x38 * id)));
    const uint8_t *row = LINN(PLAYER_ROW(me));
    for (unsigned a = 0; a < 3; a++) pl[0xc + a] = chance(1, 2) ? row[0x10 + a] : (uint8_t)(row[0x13 + a] + 1);
    const uint16_t args[] = {id, me};
    uint32_t cap = harness_call(MEM_SEL_SEG(9), 0x48cc, HCONV_CDECL, args, 2, NULL);
    static const int32_t delta[] = {-1, 0, 1};
    set_planet(id, me, 3, (uint16_t)rnd(), (uint32_t)((int32_t)cap + delta[rep % 3]));
    DIFF(24, 0x18a4, FAR_CDECL, g_dc, 1);
  }
}

/* ---- the pop-up pages: 24:0x00ca paints them, 24:0x091e measures them ---- */

#define NWIN 6
static uint16_t g_win[NWIN]; /* windows of different sizes: the client rectangle the pop-up is painted into */
static uint32_t g_fleet;     /* a fleet record at offset 0x10 of a block */
static uint32_t g_drec;      /* a ship design record at offset 0x10 of a block */

enum { KIND_MINERAL = 1, KIND_PLAYER, KIND_FLEET, KIND_PLACE, KIND_ENV, KIND_ZIP, KIND_COLONISTS, KIND_MINES, KIND_TECH, KIND_TEXT, KIND_DESIGN,
       KIND_RESOURCES, KIND_EMPTY, KIND_DESIGN_B };

/* A fleet record: id with the owner in bits 9 to 12, ship counts (some zero, some negative), damage words. */
static void world_fleet(void) {
  if (!g_fleet) g_fleet = gblock(0x200);
  uint8_t *r = (uint8_t *)LIN(g_fleet) + 0x10;
  for (unsigned i = 0; i < 0x7c; i++) r[i] = (uint8_t)rnd();
  uint16_t id = (uint16_t)(rn(0x200) | (rn(16) << 9));
  memcpy(r, &id, 2);
  for (unsigned d = 0; d < 16; d++) {
    uint16_t count = (uint16_t)(chance(1, 3) ? 0 : chance(1, 8) ? 0x8000 + rn(100) : 1 + rn(chance(1, 4) ? 30000 : 400));
    uint16_t damage = (uint16_t)(chance(1, 3) ? 0 : chance(1, 5) ? rn(0x80) : rnd());
    memcpy(r + 0xc + 2 * d, &count, 2);
    memcpy(r + 0x2c + 2 * d, &damage, 2);
  }
}
static void world_design_record(void) {
  if (!g_drec) g_drec = gblock(0x200);
  uint8_t *r = (uint8_t *)LIN(g_drec) + 0x10;
  for (unsigned i = 0; i < 0x8d; i++) r[i] = (uint8_t)rnd();
  uint16_t hull = (uint16_t)(chance(1, 3) ? 0x20 + rn(8) : rn(0x20)); /* below 0x40: 2:0x507c turns it into a hull record */
  memcpy(r, &hull, 2);
  r[0x7a] = (uint8_t)rn(17); /* the number of parts: 25:0x450c loops over them */
  snprintf((char *)r + 8, 0x20, "Hull %u", (unsigned)rn(100));
}

/* The world and the descriptor of one kind of pop-up. kind 0 and 15 and above are no page. */
static void world_kind(unsigned kind) {
  stubs_clear_all();
  world_gdi();
  world_rows(false);
  world_planets(NPLAN);
  world_designs();
  uint16_t me = (uint16_t)rn(16);
  DS_W(0x014c) = me;
  DS_W(0x4b84) = (uint16_t)rn(NPLAN);
  DS_W(0x4b7c) = (uint16_t)rn(2000);
  DS_W(0x4b7e) = (uint16_t)rn(2000);
  DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 0xc0 | rn(64) : rn(64));
  DS_W(0x0bbe) = 0;
  for (unsigned i = 0; i < 0x16; i += 2) INFO(i) = 0;
  INFO(0) = (uint16_t)kind;
  switch (kind) {
  case KIND_MINERAL:
    INFO(2) = (uint16_t)rn(6);
    INFO32(10) = chance(1, 4) ? 0xffffffffu - rn(100) : rn(100000);       /* the amount on the surface; negative = unknown */
    INFO32(14) = chance(1, 4) ? 0 : chance(1, 5) ? 0x80000000u + rn(10) : rn(chance(1, 2) ? 40 : 100); /* the concentration */
    INFO32(6) = chance(1, 2) ? 0 : 1 + rn(3);                              /* homeworld marker */
    INFO32(18) = chance(1, 5) ? 0xffffffffu - rn(10) : rn(5000);           /* the mining rate per year */
    break;
  case KIND_PLAYER:
    INFO(2) = (uint16_t)rn(16);
    break;
  case KIND_FLEET:
    world_fleet();
    INFO(2) = 0x10;
    INFO(4) = HI(g_fleet);
    INFO(6) = (uint16_t)rn(2);
    INFO(8) = (uint16_t)(chance(1, 2) ? 0 : rn(60));
    break;
  case KIND_PLACE:
    break;
  case KIND_ENV: {
    uint16_t ideal = (uint16_t)rn(101);
    uint16_t race_min = chance(1, 5) ? 0xffff : (uint16_t)rn(60);
    INFO(2) = (uint16_t)rn(NPLAN);
    INFO(4) = (uint16_t)rn(3);
    INFO(6) = value_or_none(8);
    INFO(8) = chance(1, 3) ? 0xffff : (uint16_t)rn(101);
    INFO(10) = chance(1, 3) ? 0xffff : (uint16_t)rn(101);
    INFO(12) = ideal;
    INFO(14) = race_min;
    INFO(16) = (uint16_t)(race_min == 0xffff ? rn(101) : race_min + rn(60));
    break;
  }
  case KIND_COLONISTS: {
    uint16_t id = (uint16_t)rn(NPLAN);
    INFO(2) = id;
    unsigned who = rn(5);
    uint16_t owner = who < 2 ? me : who < 4 ? (uint16_t)((me + 1 + rn(15)) & 15) : 0xffff;
    uint8_t level = (uint8_t)(chance(1, 4) ? rn(3) : 3 + rn(4));
    uint16_t population = (uint16_t)(chance(1, 3) ? rn(16) << 12 : rnd());
    set_planet(id, owner, level, population, random_colonists());
    break;
  }
  case KIND_MINES:
    INFO(2) = (uint16_t)rn(NPLAN);
    INFO(4) = (uint16_t)(chance(1, 4) ? 1 : rn(300));
    INFO(6) = (uint16_t)(chance(1, 4) ? 1 : rn(300));
    INFO(8) = (uint16_t)rn(100);
    INFO(10) = (uint16_t)rn(2);
    break;
  case KIND_TECH:
    /* a request of the tech panel that 27:0x1e06 takes the default way out of: kind 0x4000, no record */
    INFO(2) = 0x4000;
    INFO(4) = (uint16_t)rn(40);
    break;
  case KIND_TEXT:
    put_text(SCRATCH, 1 + rn(20), chance(1, 3));
    INFO(2) = (uint16_t)(0x40 + rn(0x200));
    INFO(4) = SCRATCH;
    break;
  case KIND_DESIGN:
  case KIND_DESIGN_B:
    world_design_record();
    INFO(2) = 0x10;
    INFO(4) = HI(g_drec);
    break;
  case KIND_RESOURCES: {
    uint16_t a = (uint16_t)(chance(1, 4) ? 0 : rn(2000));
    INFO(2) = (uint16_t)rn(NPLAN);
    INFO(4) = a;
    INFO(6) = (uint16_t)(chance(1, 4) ? a : rn(2000));
    break;
  }
  default:
    break;
  }
}

static void world_windows(void) {
  static const int sizes[NWIN][2] = {{260, 60}, {260, 320}, {120, 30}, {200, 110}, {400, 400}, {330, 36}};
  for (unsigned i = 0; i < NWIN; i++) g_win[i] = mkwin(WS_OVERLAPPED, 10, 10, sizes[i][0], sizes[i][1], 0, 0);
}

/* ---- 24:0x00ca: paints the pop-up ---- */

static void t_00ca(void) {
  static const unsigned order[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 15, 0xffff, 1, 3, 11, 14, 7, 5};
  for (unsigned rep = 0; rep < 36; rep++) {
    for (unsigned k = 0; k < sizeof order / sizeof order[0]; k++) {
      g_rng = 0x00ca0001u + 0x9e3779b9u * (rep * 64 + k + 1);
      world_kind(order[k]);
      uint16_t win = g_win[(rep + k) % NWIN];
      if (DIFF(24, 0x00ca, FAR_CDECL, win, g_dc) != DT_PASS) fprintf(stderr, "  00ca kind %u rep %u\n", order[k], rep);
    }
  }
}

/* ---- 24:0x091e: measures the pop-up and opens its window ---- */

static void t_091e(void) {
  static const unsigned order[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0, 15, 0xffff, 1, 3, 11, 14, 7, 5, 12, 8, 10};
  for (unsigned rep = 0; rep < 40; rep++) {
    for (unsigned k = 0; k < sizeof order / sizeof order[0]; k++) {
      g_rng = 0x091e0001u + 0x9e3779b9u * (rep * 64 + k + 1);
      world_kind(order[k]);
      uint16_t win = g_win[(rep + k) % NWIN];
      /* the mouse position: inside the window, at its corner, and far outside it (the pop-up is kept on the screen) */
      uint16_t x = (uint16_t)(chance(1, 6) ? rn(8) : chance(1, 6) ? 500 + rn(400) : rn(300));
      uint16_t y = (uint16_t)(chance(1, 6) ? rn(8) : chance(1, 6) ? 400 + rn(400) : rn(300));
      if (DIFF(24, 0x091e, FAR_CDECL, win, x, y) != DT_PASS) fprintf(stderr, "  091e kind %u rep %u\n", order[k], rep);
    }
  }
  /* boundary: the point at the origin, at the largest coordinates and at negative ones */
  static const uint16_t pts[][2] = {{0, 0}, {0xffff, 0xffff}, {0x7fff, 0x7fff}, {0x8000, 0x8000}, {639, 479}, {640, 480}, {1, 1}};
  for (unsigned i = 0; i < sizeof pts / 4; i++) {
    for (unsigned kind = 1; kind <= 14; kind++) {
      g_rng = 0x091e1000u + i * 16 + kind;
      world_kind(kind);
      DIFF(24, 0x091e, FAR_CDECL, g_win[i % NWIN], pts[i][0], pts[i][1]);
    }
  }
}

/* ---- 24:0x0e24: the pop-up menu ---- */

/* TrackPopupMenu waits for input: the hook of GetMessage feeds the keys scripted in the arena (so a
 * snapshot rewinds the script between the two runs): word 0 the next key, word 1 the number of keys,
 * the Win16 virtual-key codes from word 2 on. */
#define SCRIPT_AT 0x100
#define VK_DOWN_KEY 0x28
#define VK_ESC_KEY 0x1b
#define VK_ENTER_KEY 0x0d
#define VK_SHIFT_KEY 0x10
static bool menu_hook(void) {
  uint16_t *st = (uint16_t *)((uint8_t *)LIN(g_stub_blk) + SCRIPT_AT);
  if (st[0] >= st[1]) return false;
  uint16_t vk = st[2 + st[0]];
  uint32_t t = GetTickCount();
  st[0]++;
  rt_input_key(vk, 0x01, 1, t);
  rt_input_key(vk, 0x01, 0, t + 1);
  return true;
}
/* Script: `downs` times Down, then Return; then Escape (three times: out of a sub-menu and the menu). */
static void menu_script(unsigned downs, bool choose) {
  uint16_t *st = (uint16_t *)((uint8_t *)LIN(g_stub_blk) + SCRIPT_AT);
  unsigned n = 0;
  st[0] = 0;
  if (choose) {
    for (unsigned i = 0; i < downs && i < 20; i++) st[2 + n++] = VK_DOWN_KEY;
    st[2 + n++] = VK_ENTER_KEY;
  }
  for (unsigned i = 0; i < 3; i++) st[2 + n++] = VK_ESC_KEY; /* the last ones close a sub-menu that Return opened, and the menu */
  /* keys that change nothing, so that the menu loop never runs out of input: user_input_exhausted is not part of the
   * arena, and GetMessage that sets it in the ORIGINAL run would make the C run's loop post a quit */
  for (unsigned i = 0; i < 4; i++) st[2 + n++] = VK_SHIFT_KEY;
  st[1] = (uint16_t)n;
}

#define M_ITEMS (SCRATCH + 0x100)
#define M_LABELS (SCRATCH + 0x300)
#define M_TEXTS (SCRATCH + 0x400)
static uint16_t g_mc;          /* the number of entries */
static uint16_t g_mitems, g_mlabels;

/* A random text for a menu entry, now and then with '&'. */
static uint16_t menu_text(unsigned i) {
  uint16_t at = (uint16_t)(M_TEXTS + 0x24 * i);
  static const char *const w[] = {"Planet", "A&B", "Fleet &1", "Cancel", "Deep", "&&", "x", "Mine Field", "Very long entry of the pop-up menu"};
  snprintf((char *)LINN(at), 0x24, "%s %u", w[rn(9)], (unsigned)rn(100));
  return at;
}

/* The tables 7:0x1902 and 7:0x1a70 read: four map objects (DS:0x188e, 0x12 bytes each, the id word holds the owner and the kind of the
 * object in its high byte) and three fleets sorted by id (the far pointers DS:0xba, the count DS:0x553a; 7:0x14a8 searches them). */
static uint32_t g_mobj, g_fblk, g_fptrs;
static uint16_t g_mo_ids[4], g_fl_ids[3];
static void world_menu_objects(void) {
  if (!g_mobj) {
    g_mobj = gblock(0x100);
    g_fblk = gblock(0x400);
    g_fptrs = gblock(0x40);
  }
  static const uint16_t kind_bits[] = {0x00, 0x20, 0x40, 0x60, 0x80};
  for (unsigned k = 0; k < 4; k++) {
    uint8_t *o = (uint8_t *)LIN(g_mobj) + 0x12 * k;
    for (unsigned i = 0; i < 0x12; i++) o[i] = (uint8_t)rnd();
    uint16_t w = (uint16_t)(((kind_bits[rn(5)] | (rn(16) << 1)) << 8) | rn(256));
    memcpy(o, &w, 2);
    o[0xc] = (uint8_t)rn(3); /* an index into the three kind names at DS:0x4c2 */
    g_mo_ids[k] = w;
  }
  ds_w32(0x188e, g_mobj);
  DS_W(0x1892) = 4;
  uint16_t owners[3] = {(uint16_t)rn(16), (uint16_t)rn(16), (uint16_t)rn(16)};
  for (unsigned a = 0; a < 3; a++)
    for (unsigned b = a + 1; b < 3; b++)
      if (owners[b] < owners[a]) {
        uint16_t t = owners[a];
        owners[a] = owners[b];
        owners[b] = t;
      }
  for (unsigned k = 0; k < 3; k++) {
    uint8_t *f = (uint8_t *)LIN(g_fblk) + 0x10 + 0x100 * k;
    for (unsigned i = 0; i < 0x7c; i++) f[i] = (uint8_t)rnd();
    uint16_t id = (uint16_t)((owners[k] << 9) | (10 * k + rn(9)));
    memcpy(f, &id, 2);
    memcpy(f + 2, &owners[k], 2);
    for (unsigned d = 0; d < 16; d++) {
      uint16_t n = (uint16_t)(chance(1, 3) ? 0 : 1 + rn(40));
      memcpy(f + 0xc + 2 * d, &n, 2);
    }
    uint32_t name = 0;
    if (chance(1, 3)) {
      snprintf((char *)LIN(g_fblk) + 0x80 + 0x100 * k, 0x20, "Fleet %u", k);
      name = MK_FP(HI(g_fblk), (uint16_t)(0x80 + 0x100 * k));
    }
    memcpy(f + 0x78, &name, 4);
    g_fl_ids[k] = id;
    uint32_t fp = MK_FP(HI(g_fblk), (uint16_t)(0x10 + 0x100 * k));
    memcpy((uint8_t *)LIN(g_fptrs) + 4 * k, &fp, 4);
  }
  ds_w32(0x00ba, g_fptrs);
  DS_W(0x553a) = 3;
}

/* Builds the lists: mode 0 items only, 1 labels only, 2 labels with sub-menu groups, 3 items and labels (selected = -2). */
static void world_menu(unsigned mode, uint16_t count) {
  world_menu_objects();
  g_mc = count;
  g_mitems = 0;
  g_mlabels = 0;
  if (mode == 0 || mode == 3) {
    g_mitems = M_ITEMS;
    for (unsigned i = 0; i < count; i++) {
      uint16_t id = (uint16_t)rn(NPLAN), flags = 0;
      unsigned r = rn(9);
      if (r == 0) {
        id = 0xffff;
        flags = 0xffff;
      } else if (r == 1) {
        flags = 0x1000;
      } else if (r == 2) {
        static const uint16_t short_strings[] = {0x255, 0x256, 0x25d, 0x22d, 0x22e, 0x230, 0x232, 0x234, 0x336, 0x24e, 0x24b, 0x249};
        id = short_strings[rn(12)]; /* a short string: the original copies the text to a buffer of 152 bytes */
        flags = 0x4000;
      } else if (r == 3) {
        flags = 0x2000; /* 7:0x1902 names the map object with this id word */
        id = chance(1, 5) ? 0x1234 : g_mo_ids[rn(4)];
      } else if (r == 4) {
        flags = 0x8000; /* 7:0x1a70 names the fleet with this id (the code sets bit 15 of it) */
        id = chance(1, 5) ? 0x0777 : g_fl_ids[rn(3)];
      } else if (mode == 3) {
        flags = (uint16_t)rn(2); /* the checked flag of a sub-menu item */
      }
      memcpy(LINN((uint16_t)(M_ITEMS + 4 * i)), &id, 2);
      memcpy(LINN((uint16_t)(M_ITEMS + 4 * i + 2)), &flags, 2);
    }
  }
  if (mode >= 1) {
    g_mlabels = M_LABELS;
    unsigned i = 0;
    while (i < count) {
      uint16_t w;
      if (mode >= 2 && chance(1, 4) && i + 2 < count) { /* a group: 0, the title, then entries up to the next 0 */
        w = 0;
        memcpy(LINN((uint16_t)(M_LABELS + 2 * i)), &w, 2);
        w = menu_text(i);
        memcpy(LINN((uint16_t)(M_LABELS + 2 * i + 2)), &w, 2);
        i += 2;
        continue;
      }
      if (chance(1, 6)) {
        w = (uint16_t)(M_TEXTS + 0x24 * 30); /* "\xff\0": a separator */
        ((uint8_t *)LINN(w))[0] = 0xff;
        ((uint8_t *)LINN(w))[1] = 0;
      } else {
        w = menu_text(i);
      }
      memcpy(LINN((uint16_t)(M_LABELS + 2 * i)), &w, 2);
      i++;
    }
  }
}

static void t_0e24(void) {
  user_wait_hook = menu_hook;
  DS_W(0x2806) = g_win[0];
  CHECK(SetFocus(g_win[0]) != 0 || GetFocus() == g_win[0]); /* the keys of the script go to the focus window */
  for (unsigned it = 0; it < 500; it++) {
    g_rng = 0x0e240001u + 0x9e3779b9u * (it + 1);
    stubs_clear_all();
    world_gdi();
    world_rows(false);
    world_planets(NPLAN);
    DS_W(0x2806) = g_win[0];
    DS_W(0x4c84) = (uint16_t)rnd();
    unsigned mode = it % 4;
    uint16_t count = (uint16_t)(chance(1, 12) ? 1 : 1 + rn(10));
    world_menu(mode, count);
    int16_t selected = (int16_t)(mode == 3 ? -2 : chance(1, 3) ? -1 : (int)rn(count));
    if (mode == 3 && chance(1, 4)) selected = (int16_t)rn(count);
    menu_script(rn(count), chance(3, 4));
    uint16_t x = (uint16_t)rn(300), y = (uint16_t)rn(200);
    if (DIFF(24, 0x0e24, FAR_CDECL, g_win[(it / 4) % NWIN], x, y, g_mc, g_mitems, g_mlabels, (uint16_t)selected, it & 1) != DT_PASS) {
      fprintf(stderr, "  0e24 iteration %u mode %u count %u\n", it, mode, count);
    }
  }
  /* boundary: no entries, the selection -2 without lists, the selected index past the count */
  g_rng = 0x0e24a000u;
  stubs_clear_all();
  menu_script(0, true);
  DIFF(24, 0x0e24, FAR_CDECL, g_win[0], 5, 5, 0, 0, 0, 0xffff, 0);
  world_menu(1, 3);
  menu_script(1, true);
  DIFF(24, 0x0e24, FAR_CDECL, g_win[0], 0, 0, 3, 0, g_mlabels, 0xfffe, 1);
  DIFF(24, 0x0e24, FAR_CDECL, g_win[0], 0xffff, 0xffff, 3, 0, g_mlabels, 7, 0);
  user_wait_hook = NULL;
}

/* ---- 24:0x0000: POPUPWNDPROC ---- */

static void t_0000(void) {
  uint16_t popup = mkwin(WS_OVERLAPPED, 20, 20, 150, 80, 0, 0); /* the window DS:0x0bbe that a button release closes */
  static const uint16_t messages[] = {0x0001, 0x000f, 0x0014, 0x0202, 0x0205, 0x0020, 0x0111, 0x0007, 0x0200, 0x0201, 0x0204, 0x0000, 0x0010, 0xffff};
  for (unsigned it = 0; it < 420; it++) {
    g_rng = 0x00000001u + 0x9e3779b9u * (it + 1);
    unsigned kinds[] = {2, 4, 6, 8, 9, 10, 11, 13, 14, 1, 3, 5, 7, 12};
    world_kind(kinds[rn(14)]);
    uint16_t msg = messages[it % 14];
    DS_W(0x0bbe) = (uint16_t)(chance(2, 3) ? popup : 0);
    DS_B(0x0791) = (uint8_t)(chance(1, 2) ? 8 | rn(8) : rn(8));
    DS_B(0x53f1) = (uint8_t)rnd();
    uint16_t hwnd = g_win[rn(NWIN)];
    uint32_t lparam = rnd();
    DIFF(24, 0x0000, FAR_PASCAL, hwnd, msg, g_dc, HI(lparam), LO(lparam)); /* PASCAL: lParam high word first */
  }
}

int main(void) {
  g_only = getenv("SEG24_ONLY");
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "harness_load failed\n");
    return 1;
  }
  kernel_register();
  if (gdi_init() != 0 || user_init() != 0) {
    fprintf(stderr, "gdi/user init failed\n");
    return 1;
  }
  gdi_register();
  user_register();
  register_test_class("SEG24TEST");
  register_test_class("starspopup");
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* the string and font helpers use LocalAlloc */
  stubs_init();
  /* The inventory (docs/functions.csv) ends 24:0x00ca, 0x091e, 0x23ba and 0x25a2 before their jump tables and
   * cases (docs/code-segment-data.md lists the rest as part of these functions): measure the coverage over
   * the whole function. The linear sweep of the coverage report stops at the jump tables; the executed
   * offsets listed in build/coverage/24_<off>.txt are exact. */
  difftest_set_extent(24, 0x00ca, 0x854, NULL);
  difftest_set_extent(24, 0x091e, 0x506, NULL);
  difftest_set_extent(24, 0x23ba, 0x1e8, NULL);
  difftest_set_extent(24, 0x25a2, 0x196, NULL);
  rt_set_mode(RT_MODE_DECOMP); /* the C version reaches the decompiled functions (this file, 27:0x1e06) as C: rt_call_far runs the registry's C */
  g_dc = gdi_create_screen_dc();
  CHECK(g_dc != 0);
  if (want("2738")) t_2738();
  if (want("208c")) t_208c();
  if (want("25a2")) t_25a2();
  if (want("23ba")) t_23ba();
  if (want("116a")) t_116a();
  if (want("18a4")) {
    t_18a4();
    t_18a4_capacity_edge();
  }
  if (want("00ca") || want("091e") || want("0000")) world_windows();
  if (want("00ca")) t_00ca();
  if (want("091e")) t_091e();
  if (want("0000")) t_0000();
  if (want("0e24")) {
    if (!g_win[0]) world_windows();
    t_0e24();
  }
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
