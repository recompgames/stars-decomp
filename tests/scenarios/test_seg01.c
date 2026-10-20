/* Diff tests for decompiled segment 1 (src/seg01.c). Public (docs/PLAN.md decision 8).
 *
 * Segment 1 is the game's start-up and its configuration reader:
 *   1:0x0000  start-up: screen class, colours, pens, brushes, bitmaps, icons, cursors, procedure instances
 *   1:0x08a4  build the fonts and measure the text cells
 *   1:0x0af8  create the main window and load the accelerator tables
 *   1:0x0bb4  lay out the two tables of tool buttons (6 and 7 entries)
 *   1:0x0c36  read one "x,y,w,h" window placement from the ini file
 *   1:0x0d90  read the whole ini file into DGROUP
 *   1:0x1da0  apply a letter string ("ABc*") to a table of 16-byte entries
 * Every scenario builds its state by writing DGROUP directly; DIFF then snapshots it and compares
 * the original with the C version (return value, all memory, the ordered Win16 API calls).
 * See the comment above each scenario for what it does and does not reach. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/difftest.h"
#include "harness/gdi_thunks.h"
#include "harness/kernel_thunks.h"
#include "harness/user_thunks.h"
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/user.h"
#include "runtime/vfs.h"
#include "src/stars.h"
#include "test.h"

#define EXE "original/stars26i.exe"
#define DS_W(off) (*(uint16_t *)LINN(off))
#define DS_B(off) (*(uint8_t *)LINN(off))
#define LO(p) ((uint16_t)(p))
#define HI(p) ((uint16_t)((p) >> 16))

static const char *g_only;
static bool want(const char *tag) { return !g_only || strstr(g_only, tag) != NULL; }

static uint32_t g_rng = 0x5EED0001u;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13;
  g_rng ^= g_rng >> 17;
  g_rng ^= g_rng << 5;
  return g_rng;
}

/* ---- 1:0x1da0: apply a letter string to a table of 16-byte entries ---- */

/* The string is at DS:str, the table at DS:tab with n entries; byte 10 of an entry holds a 3-bit value
 * (bits 0-2), a 4-bit key (bits 3-6, the letter - 'A' or - 'a') and the case bit 7. DS:0x1955 is the
 * C runtime's character-class table (bit 0 upper, bit 1 lower). Random strings over letters, "*",
 * digits, spaces and bytes above 0x7f against random tables. */
static void t_1da0(void) {
  const uint16_t str = 0x5866;
  static const uint8_t alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz**12 -\x80\xc1\xff\x01";
  const unsigned na = sizeof alpha - 1;
  /* fixed cases first: empty string, a lone star, two stars, a single letter, the empty table */
  static const char *fixed[] = {"", "*", "**", "A", "a", "*A", "A*", "aBcD", "ZzYy", "*aB*c", "AA", "aA", "1", "\x80", "Q", "q", "P", "p"};
  const uint16_t tabs[] = {0x07c2, 0x08d6};
  const uint16_t counts[] = {6, 7, 0, 1, 2, 0xFFFF, 0x8000, 16};
  for (unsigned f = 0; f < sizeof fixed / sizeof fixed[0]; f++) {
    for (unsigned c = 0; c < sizeof counts / 2; c++) {
      for (unsigned t = 0; t < 2; t++) {
        for (unsigned i = 0; i < 0x120; i++) DS_B(tabs[t] + i) = (uint8_t)rnd();
        strcpy((char *)LINN(str), fixed[f]);
        DIFF(1, 0x1da0, FAR_CDECL, str, tabs[t], counts[c]);
      }
    }
  }
  for (unsigned it = 0; it < 600; it++) {
    unsigned n = (it & 1) ? 7 : 6;
    uint16_t tab = (n == 7) ? 0x08d6 : 0x07c2;
    for (unsigned i = 0; i < n * 16; i++) DS_B(tab + i) = (uint8_t)rnd();
    if (it % 4 == 0) /* keys that do collide: letters already present in the table */
      for (unsigned e = 0; e < n; e++) DS_B(tab + 16 * e + 10) = (uint8_t)((rnd() & 0x87) | ((rnd() % 9) << 3));
    unsigned len = rnd() % 14;
    for (unsigned i = 0; i < len; i++) DS_B(str + i) = alpha[rnd() % na];
    DS_B(str + len) = 0;
    DIFF(1, 0x1da0, FAR_CDECL, str, tab, n);
  }
}

/* A zeroed, locked global block; returns its far pointer. */
static uint32_t gblock(uint16_t size) {
  uint16_t h = GlobalAlloc(0x42, size); /* GMEM_MOVEABLE | GMEM_ZEROINIT */
  CHECK(h != 0);
  return GlobalLock(h);
}

/* ---- ini file helpers ---- */

/* The ini file the game reads is "Stars.ini" in C:\WINDOWS (the virtual file system of the runtime). The
 * section and file names are strings in DGROUP, as the game passes them (near pointers). */
#define INI_APP 0x5900
#define INI_FILE 0x5920
static void put_ini(const char *text) { CHECK_EQ(vfs_put("C:\\WINDOWS\\STARS.INI", text, strlen(text)), 0); }
static void set_ini_names(void) {
  strcpy((char *)LINN(INI_APP), "Windows");
  strcpy((char *)LINN(INI_FILE), "Stars.ini");
}

/* ---- 1:0x0c36: read one window placement (four numbers and the mode letter) from the ini file ---- */

/* The value must be exactly 17 characters: M, R or I, then four 4-character numbers (digits; a "-" in
 * any position marks the number negative and is otherwise skipped, so "-123" is -123 and "1-23" is
 * 123 negated). Anything else gives the default placement {0x8000, 0, 0x8000, 0} with the flag bit 0
 * set only for the key "Main" (string id 0x94). The result is eight bytes at the destination plus three
 * flag bits in the byte after them (bit 0 = "M", bit 1 = "I", bit 2 = parsed). Ids 0x94 and 0xb2 name the
 * keys "Main" and "ReportFleetWin". */
static void t_0c36(void) {
  static const char *const vals[] = {
      "M0010002003000400", "R0010002003000400", "I0010002003000400", "M-001-002-003-004", "M1-231-231-231-23",
      "M9999999999999999", "M0000000000000000", "X0010002003000400", "m0010002003000400", "M001000200300040",
      "M00100020030004000", "M0010002003000x00", "M00 0002003000400", "M0010002003000:00", "M/010002003000400",
      "M----------------", "I-0-0-0-0-0-0-0-0", "R5555555555555555", "M9-9-9-9-9-9-9-9-", ""};
  set_ini_names();
  const uint16_t ids[] = {0x94, 0xb2};
  for (unsigned v = 0; v < sizeof vals / sizeof vals[0]; v++) {
    char buf[96];
    snprintf(buf, sizeof buf, "[Windows]\r\nMain=%s\r\nReportFleetWin=%s\r\n", vals[v], vals[(v + 3) % (sizeof vals / sizeof vals[0])]);
    put_ini(buf);
    for (unsigned k = 0; k < 2; k++) {
      for (unsigned flags = 0; flags < 256; flags += 0x55) {
        for (unsigned i = 0; i < 12; i++) DS_B(0x2510 + i) = (uint8_t)(0xA5 ^ i);
        DS_B(0x2518) = (uint8_t)flags;
        DIFF(1, 0x0c36, FAR_CDECL, INI_APP, INI_FILE, ids[k], 0x2510);
      }
    }
  }
  /* a key that is not in the file: the default value (the default string of the original) */
  put_ini("[Windows]\r\nOther=1\r\n");
  DIFF(1, 0x0c36, FAR_CDECL, INI_APP, INI_FILE, 0x94, 0x2510);
  DIFF(1, 0x0c36, FAR_CDECL, INI_APP, INI_FILE, 0xb3, 0x2510);
}

/* ---- 1:0x08a4: create the fonts and measure the text cells ---- */

/* The four face names of the font table at DS:0x06ca (32 bytes each) are loaded from the string table
 * (ids 0x51b to 0x51e: Arial, Arial Bold, Arial Italic, Arial Bold Italic) where the first byte is 0;
 * names already present are kept. Then eight CreateFontIndirect calls make the fonts at DS:0x4a5a
 * (names 0 and 1, 10 pt), DS:0x293a (name 0, 6 pt), DS:0x2938 (name 0, 7 pt), DS:0x292e to 0x2934
 * (the four names, 8 pt) and DS:0x2936 (name 1, escapement 0xc4e), and the cell sizes DS:0x267a,
 * 0x2676, 0x2678, 0x54ee (height + external leading of four fonts) and DS:0x288a (width of 8
 * characters at DS:0x74a) are stored. Scenarios: no names, every name preset, a mix, a long name, and
 * two different texts to measure. The device context is the runtime's screen. */
static void t_08a4(void) {
  uint16_t dc = gdi_create_screen_dc();
  CHECK(dc != 0);
  static const char *const names[] = {"", "Arial", "Courier", "Arial Bold", "Times New Roman Italic Tiny", "A"};
  for (unsigned it = 0; it < 14; it++) {
    for (unsigned f = 0; f < 4; f++) {
      const char *nm = names[(it < 6) ? (it + f) % 6 : rnd() % 6];
      if (it == 0) nm = "";
      if (it == 1) nm = "Arial";
      memset(LINN(0x06ca + 0x20 * f), 0, 0x20);
      strcpy((char *)LINN(0x06ca + 0x20 * f), nm);
    }
    for (unsigned i = 0; i < 8; i++) DS_B(0x74a + i) = (it & 1) ? (uint8_t)('0' + i) : (uint8_t)(0x38);
    DIFF(1, 0x08a4, FAR_CDECL, dc);
  }
}

/* ---- 1:0x0d90: read the whole ini file ---- */

/* A generated Stars.ini: every key the function reads, each absent (one time in five) or with a value
 * drawn from a pool made for it (numbers around the limits it clamps to, letter strings, malformed
 * values). 1:0x0d90 itself runs through the real helpers (1:0x0c36, 1:0x1da0, 8:0x8dc0, the string table, the
 * C runtime's atoi and date call, 4:0x1c78 and 12:0x0232), in both runs. */
static const char *const P_INT[] = {"0", "1", "2", "3", "4", "5", "9", "10", "15", "16", "99", "100", "101", "200", "255",
                                    "256", "257", "512", "1000", "2000", "2001", "3000", "30000", "30001", "32767", "32768",
                                    "40000", "65535", "-1", "-5", "-32768", "abc", ""};
static const char *const P_PLACE[] = {"M0010002003000400", "R0100020005000600", "I-001-002-003-004", "X0010002003000400",
                                      "M123", "M0000000000000000", "M0050001000300020", "R9999999999999999", ""};
static const char *const P_SEL[] = {"", "S", "SB", "SBx", "SB123", "Sa", "E", "EQ1", "EQ", "Pc7", "PR", "Pb9", "Xx", "Sc12",
                                    "S\x80", "Eb1", "Pq", "N", "Xxy", "Nbq", "Zbb", "Eaa", "S\x80\x80"};
static const char *const P_GLOBAL[] = {"abcdefghijklmnopqrstuvwxyz12", "abcdefghijklmnopqrstuvwxy12", "", "X",
                                       "0123456789012345678901234567"};
static const char *const P_TILES[] = {"", "A", "AbC*d", "*", "**", "aBcDeF", "Z*a", "AA", "ab", "abcdefghijklmnop", "*ABCDEFGH", "12", "A B C", "F*f*"};
static const char *const P_GAMEID[] = {"0123456789", "abcdef", "ABCDEF", "12g4", "", "00000000ff", "deadbeef"};
static const char *const P_FILE[] = {"", "game.m1", "x", "abc", "abcd", "C:\\STARS\\GAME.HST", "a\\b\\c.xy"};
static const char *const P_FONT[] = {"", "Arial", "Courier New", "abcd", "abcde", "Times New Roman Italic Narrow", "This font name is exactly 31 c", "This font name is exactly 32 ch"};
static const char *const P_ZIP[] = {"abcdefghijklmnopabcdName", "abcdefghijklmnopabcd", "abcdefghijklmnopabc", "abcdefghijklmnopabcdefghijklmnopq",
                                    "abcdefghijklmnopabcdTwelveChars", "abcdefghijklmnopabcdThirteenChars", "abcdefghijklmnopabcQ", "ABCDEFGHIJKLMNOPABCD",
                                    "aaaaaaaaaaaaaaaaaaaaX", "ppppppppppppppppppppZ", "abcdefghijklmnopabcdefghijklmnopabcdefghijklmnop", ""};
static const char *const P_ZIPP[] = {"aa", "baabcd", "aaxyz", "bbabcdNamed", "bcabcdefgh", "cdabcdefghijklmnop", "bmabcdefghijklmnopabcdefghijklmnopabcdefghijklmnopabcdefg",
                                     "bnabcdefghijklmnopabcdefghijklmnopabcdefghijklmnopabcdefg", "bbapcd", "bbpppp", "bbaaaa", "bcpppppppp", "bcdddddddd", "bbppppN",
                                     "bbhaaa", "bbiaaa", "bbgaaa", "bbhbaa", "bbhpaa", "bchaaaiaaa", "bbzzzz", "bAabcd", "b`abcd", "babcdNametwelvech", "babcdName", "bb!!!!", "bcaaaaaaaaWOW", "bcabcdefghLongNameXXXXX", "bbA"};
#define NPOOL(a) (sizeof(a) / sizeof((a)[0]))

typedef struct IniKey {
  const char *key;
  const char *const *pool;
  unsigned n;
} IniKey;
#define K(name, pool) {name, pool, NPOOL(pool)}

static char g_ini[16384];
static unsigned g_ini_len;
static void ini_add(const char *fmt, const char *a, const char *b) {
  g_ini_len += (unsigned)snprintf(g_ini + g_ini_len, sizeof g_ini - g_ini_len, fmt, a, b);
}
static void ini_section(const char *sect, const IniKey *keys, unsigned nkeys, unsigned absent_in) {
  ini_add("[%s]\r\n%s", sect, "");
  for (unsigned k = 0; k < nkeys; k++) {
    if (absent_in && rnd() % absent_in == 0) continue;
    ini_add("%s=%s\r\n", keys[k].key, keys[k].pool[rnd() % keys[k].n]);
  }
}
static void make_ini(unsigned absent_in) {
  static const IniKey win[] = {K("Main", P_PLACE), K("ReportFleetWin", P_PLACE), K("ReportEFleetWin", P_PLACE),
      K("ReportBtlWin", P_PLACE), K("ReportPlanWin", P_PLACE), K("Resolution", P_INT), K("Layout", P_INT),
      K("Style1Width", P_INT), K("Style1Height", P_INT), K("Style1Height2", P_INT), K("Style2Width", P_INT),
      K("Style2Height", P_INT), K("Style2Height2", P_INT), K("Toolbar", P_INT), K("GlobalSettings", P_GLOBAL),
      K("PlanetTiles", P_TILES), K("ShipTiles", P_TILES), K("Selection", P_SEL), K("Message", P_INT), K("GameID", P_GAMEID),
      K("ScanZoom", P_INT), K("ScanFilterV25", P_INT), K("ScanEFilterV25", P_INT), K("ScanMines", P_INT),
      K("ScanRadar", P_INT), K("ScanModeV25", P_INT), K("MineralScale", P_INT)};
  static const IniKey files[] = {K("Wait", P_INT), K("File1", P_FILE), K("File2", P_FILE), K("File3", P_FILE), K("File4", P_FILE),
      K("File5", P_FILE), K("File6", P_FILE), K("File7", P_FILE), K("File8", P_FILE), K("File9", P_FILE), K("Turn", P_INT)};
  static const IniKey misc[] = {K("DefaultPassword", P_FILE), K("Progress", P_INT), K("Backups", P_INT), K("NewReports", P_INT),
      K("NoHostNames", P_INT), K("ReportPlanFld", P_INT), K("ReportPlanSort", P_INT), K("ReportFleetFld", P_INT),
      K("ReportFleetSort", P_INT), K("ReportEFleetFld", P_INT), K("ReportEFltSort", P_INT), K("ReportBtlFld", P_INT),
      K("ReportBtlSort", P_INT), K("ReportDefGraph", P_INT), K("HistoryInfo", P_INT), K("VCRSpeed", P_INT)};
  static const IniKey fonts[] = {K("Arial", P_FONT), K("ArialBold", P_FONT), K("ArialItalic", P_FONT), K("ArialBoldItalic", P_FONT)};
  static const IniKey zip[] = {K("ZipOrders1", P_ZIP), K("ZipOrders2", P_ZIP), K("ZipOrders3", P_ZIP), K("ZipOrders4", P_ZIP),
      K("ZipOrdersP1", P_ZIPP), K("ZipOrdersP2", P_ZIPP), K("ZipOrdersP3", P_ZIPP), K("ZipOrdersP4", P_ZIPP), K("ZipOrdersP5", P_ZIPP)};
  g_ini_len = 0;
  g_ini[0] = 0;
  ini_section("Windows", win, NPOOL(win), absent_in);
  ini_section("Files", files, NPOOL(files), absent_in);
  ini_section("Misc", misc, NPOOL(misc), absent_in);
  ini_section("Fonts", fonts, NPOOL(fonts), absent_in);
  ini_section("ZipOrders", zip, NPOOL(zip), absent_in);
  put_ini(g_ini);
}

static uint32_t g_block;
static void t_0d90_state(unsigned it) {
  /* game state the function merges its settings into: random bits in the words it masks */
  DS_W(0x251a) = (uint16_t)rnd();
  DS_W(0x0792) = (uint16_t)rnd();
  DS_W(0x0794) = (uint16_t)rnd();
  DS_W(0x0796) = (uint16_t)rnd();
  DS_B(0x0791) = (it & 1) ? 0xC0 : (uint8_t)rnd();   /* 800x600 or larger */
  DS_W(0x54f6) = (it & 2) ? 5 : (uint16_t)(rnd() % 8);
  DS_W(0x04c8) = (uint16_t)rnd();
  DS_W(0x0082) = (uint16_t)rnd();
  DS_W(0x055e) = (uint16_t)rnd();
  DS_W(0x0560) = (uint16_t)rnd();
  DS_W(0x0576) = (uint16_t)rnd();
  DS_W(0x0872) = (uint16_t)rnd();
  DS_W(0x0874) = (uint16_t)rnd();
  DS_W(0x2510) = (uint16_t)rnd();
  DS_W(0x2518) = (uint16_t)rnd();
  /* the 256-byte entries of the file list: reuse the block of an earlier run, or let the function allocate */
  if (it % 3 == 0) { DS_W(0x05f6) = 0; DS_W(0x05f8) = 0; }
  else { DS_W(0x05f6) = LO(g_block); DS_W(0x05f8) = HI(g_block); }
}

static void t_0d90(void) {
  set_ini_names();
  g_block = gblock(0x900);
  for (unsigned it = 0; it < 90; it++) {
    make_ini(it < 6 ? 0 : (it % 4 == 0 ? 2 : 5));
    t_0d90_state(it);
    DIFF(1, 0x0d90, FAR_CDECL);
  }
  /* no ini file at all, and an empty one */
  CHECK_EQ(vfs_unlink("C:\\WINDOWS\\STARS.INI"), 0);
  t_0d90_state(0);
  DIFF(1, 0x0d90, FAR_CDECL);
  put_ini("");
  t_0d90_state(1);
  DIFF(1, 0x0d90, FAR_CDECL);
}

/* ---- 1:0x0af8: create the main window and load the two accelerator tables ---- */

/* A window class named like the game's main window ("starsframe", DS:0x0196) whose procedure answers every
 * message with 1 (3 instructions of test code: MOV AX,1; XOR DX,DX; RETF 10), so creation is not vetoed and
 * nothing else happens. */
static void register_frame_class(uint16_t hinst) {
  static const uint8_t code[] = {0xB8, 0x01, 0x00, 0x31, 0xD2, 0xCA, 0x0A, 0x00};
  uint16_t sel = harness_alloc_code(code, sizeof code);
  CHECK(sel != 0);
  uint32_t wc = gblock(sizeof(WNDCLASS16));
  WNDCLASS16 *w = LIN(wc);
  w->lpfnWndProc = MK_FP(sel, 0);
  w->lpszClassName = MK_FP(MEM_SEL_DGROUP, 0x0196);
  w->hInstance = hinst;
  CHECK(RegisterClass(wc) != 0);
}

/* The function first runs the ini reader 1:0x0d90 (real, in both runs), then CreateWindow(class "starsframe",
 * title "Stars!", WS_OVERLAPPEDWINDOW, the rectangle DS:0x2510), LoadAccelerators 0x74 and 0x438, and shows
 * the window with nCmdShow (1 becomes 3 when the placement flags DS:0x2518 & 3 are set) and then hides it
 * again (ShowWindow(h, 0)). Returns 0 when the window or an accelerator table cannot be had.
 * NOT covered: the failure of the second LoadAccelerators (the first one would have to succeed
 * and the second fail, which the real resource table does not allow). */
static void t_0af8(void) {
  set_ini_names();
  g_block = gblock(0x900);
  /* class not registered: CreateWindow fails and the function returns 0 */
  make_ini(5);
  t_0d90_state(1);
  DS_W(0x54f4) = 0x0118;
  DIFF(1, 0x0af8, FAR_CDECL, 1);
  register_frame_class(0x0118);
  static const uint16_t shows[] = {1, 3, 0, 7, 5, 2, 0xFFFF, 11};
  for (unsigned it = 0; it < 24; it++) {
    make_ini(it % 3 == 0 ? 0 : 6);
    t_0d90_state(it + 1);
    DS_W(0x54f4) = 0x0118;
    DS_W(0x251a) = (uint16_t)rnd();
    DIFF(1, 0x0af8, FAR_CDECL, shows[it % 8]);
  }
  /* a placement saved with the window restored (mode letter R): the flags DS:0x2518 & 3 are 0 and 1 stays 1 */
  put_ini("[Windows]\r\nMain=R0010002003000400\r\n");
  t_0d90_state(3);
  DS_W(0x54f4) = 0x0118;
  DIFF(1, 0x0af8, FAR_CDECL, 1);
  put_ini("[Windows]\r\nMain=I0010002003000400\r\n");
  DIFF(1, 0x0af8, FAR_CDECL, 1);
  /* a module that has no resources: the window can be created, the accelerator table cannot */
  make_ini(5);
  t_0d90_state(2);
  DS_W(0x54f4) = 0x1234;
  DIFF(1, 0x0af8, FAR_CDECL, 1);
}

static void load_module(void);

/* ---- 1:0x0000: the start-up ---- */

/* A copy of the module with the RT_BITMAP resources damaged in the NE resource table. only = 0: the bitmap
 * type becomes 0xf2, so every LoadBitmap fails and the failure branch of 1:0x0000 runs with every check
 * failing at once. only = an id: the bitmap with that id alone gets another id, so exactly that one
 * LoadBitmap / load_res fails and the return value and the box show that check alone. */
static void load_module_lacking(uint16_t only) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  uint32_t ne;
  uint16_t res;
  unsigned hit = 0;
  memcpy(&ne, img + 0x3c, 4);
  memcpy(&res, img + ne + 0x24, 2);
  for (uint8_t *t = img + ne + res + 2;;) { /* TYPEINFO: type, count, reserved (4), then 12 bytes per resource */
    uint16_t type, count;
    memcpy(&type, t, 2);
    memcpy(&count, t + 2, 2);
    if (type == 0) break;
    if (type == 0x8002 && only == 0) {
      type = 0x80f2;
      memcpy(t, &type, 2);
      hit++;
    } else if (type == 0x8002) {
      for (unsigned r = 0; r < count; r++) { /* NAMEINFO: offset, length, flags, id, handle, usage */
        uint8_t *ni = t + 8 + 12u * r;
        uint16_t id;
        memcpy(&id, ni + 6, 2);
        if (id == (uint16_t)(0x8000u | only)) {
          id = (uint16_t)(0x8000u | (only + 0x3000u));
          memcpy(ni + 6, &id, 2);
          hit++;
        }
      }
    }
    t += 8 + 12u * count;
  }
  CHECK(hit != 0);
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}

/* The solid brushes, pens, pattern brushes, cursors, bitmaps, icons and string/bitmap holders it loads are
 * the real ones (resources of the module, the GDI and USER of the runtime, and 8:0xa4ae, 8:0xad28,
 * 8:0xa910, 8:0xb6b6, 12:0x0232 in both runs), and so is the message box 8:0x8d94 of the failure branch
 * (primed by the warm-up call in main; its MessageBox call, with the GetFocus and the text, is in the API
 * log). The runtime reports an 800x600 screen, so the other display classes (1024x768, 1111x888 and up, below 800x600) are NOT covered. */
static void t_0000(void) {
  DS_W(0x54f4) = 0x0118; /* hInstance */
  for (unsigned it = 0; it < 6; it++) {
    DS_W(0x0790) = (uint16_t)rnd();
    DS_B(0x0791) = (uint8_t)rnd();
    for (unsigned k = 0; k < 0xc0; k++) DS_B(0x51b4 + k) = (uint8_t)rnd();
    DIFF(1, 0x0000, FAR_CDECL);
  }
  load_module_lacking(0);
  DIFF(1, 0x0000, FAR_CDECL);
  /* each check alone: a module that lacks exactly one bitmap. 0x86 is the 'DS:0x2884 != 0' success check
   * only; 0x70, 0x57, 0xb2 are the first failed-flag test, 0x228.. 0x22c / 0x22d..0x231 / 0x1f4..0x1fa the
   * three load loops (0x22d and 0x231 are the ends of the second one), 0x85, 0x50, 0x4f, 0x77, 0xc7, 0x58
   * 0x77, 0xc7, 0x58 the other success checks (resources loaded by 8:0xad28 or LoadBitmap). */
  static const uint16_t lacking[] = {0x86, 0x22d, 0x231, 0x228, 0x22c, 0x1f4, 0x1fa, 0x70, 0x57, 0xb2,
                                     0x85, 0x50, 0x4f, 0x77, 0xc7, 0x58};
  for (unsigned k = 0; k < sizeof lacking / sizeof lacking[0]; k++) {
    load_module_lacking(lacking[k]);
    DS_W(0x54f4) = 0x0118;
    DIFF(1, 0x0000, FAR_CDECL);
  }
  load_module();
}

/* ---- 1:0x0bb4: lay out the two tables of tool buttons ---- */

/* Two tables of 16-byte entries, 6 at DS:0x07c2 and 7 at DS:0x08d6. Per entry: the word at +0 (the
 * running position), the word at +2 (accumulated), the byte at +10 (class in bits 0-2, bit 7 = fixed
 * width) and the byte at +11 (bits 3 and 4 are cleared). The cell width is the word DS:0x267a. Random
 * tables and cell widths, including 0, negative and large values (the product wraps in 16 bits). */
static void t_0bb4(void) {
  static const uint16_t cells[] = {0, 1, 7, 0x10, 0x7fff, 0x8000, 0xffff, 0x1234};
  for (unsigned it = 0; it < 300; it++) {
    for (unsigned i = 0; i < 6 * 16; i++) DS_B(0x07c2 + i) = (uint8_t)rnd();
    for (unsigned i = 0; i < 7 * 16; i++) DS_B(0x08d6 + i) = (uint8_t)rnd();
    if (it % 3 == 0) /* few distinct classes, so that runs of equal classes occur */
      for (unsigned e = 0; e < 13; e++) {
        uint16_t a = (uint16_t)((e < 6) ? 0x07c2 + 16 * e : 0x08d6 + 16 * (e - 6));
        DS_B(a + 10) = (uint8_t)((DS_B(a + 10) & 0x80) | (rnd() % 3));
      }
    DS_W(0x267a) = (it < 8) ? cells[it] : (uint16_t)(rnd() % (it % 2 ? 40 : 0x10000));
    DIFF(1, 0x0bb4, FAR_CDECL);
  }
}

/* The module itself: the resources (bitmaps, icons, cursors, accelerator tables, strings) come from it. */
static void load_module(void) {
  FILE *fp = fopen(EXE, "rb");
  CHECK(fp != NULL);
  if (!fp) return;
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  uint8_t *img = malloc((size_t)len);
  CHECK(img != NULL && fread(img, 1, (size_t)len, fp) == (size_t)len);
  fclose(fp);
  CHECK_EQ(kernel_load(img, (size_t)len), 0);
  free(img);
}

int main(void) {
  g_only = getenv("SEG01_ONLY");
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
  load_module();
  mem_local_init(harness_stack_top(), (uint16_t)(harness_stack_top() + 8192u)); /* LocalAlloc needs the local heap */
  /* Warm-up: primes the runtime's message-box state (the first call of 8:0x8d94 leaves state behind that the
   * arena restore between the two runs does not reproduce), so that the real routine 8:0x8d94 runs
   * identically in the ORIGINAL run and the C run. */
  (void)MessageBox(0, MK_FP(MEM_SEL_DGROUP, 0x1529), MK_FP(MEM_SEL_DGROUP, 0x1529), 0x10);
  if (want("0000")) t_0000();
  if (want("0d90")) t_0d90();
  if (want("0af8")) t_0af8();
  if (want("08a4")) t_08a4();
  if (want("0c36")) t_0c36();
  if (want("0bb4")) t_0bb4();
  if (want("1da0")) t_1da0();
  int rc = difftest_summary();
  harness_shutdown();
  return rc | TEST_DONE();
}
