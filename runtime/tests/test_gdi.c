/* Expectations for the GDI core (runtime/gdi*.c, runtime/framebuffer.c), issue stars-0z4.4.
 * Written before the implementation. Do not edit an expectation to match the code: a
 * disagreement is reported with both values.
 *
 * Surfaces are 8x8 or 16x16 memory DCs, white (index 255) unless stated. Grid legends:
 *   '.' index 255 (white)   '#' index 0 (black)   'r' index 249 (red)   'b' index 252 (blue)
 *   's' index 7 (c0c0c0)    'g' index 248 (808080)
 * Indices are the system palette: the 20 static colours at 0-9 and 246-255 (Windows order).
 * Where an expectation comes from the Windows 3.1 documentation or Wine rather than from a
 * reference capture (none exist yet, decision 9), the comment says so.
 */
#include "test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/harness.h"
#include "harness/gdi_thunks.h"
#include "runtime/gdi.h"
#include "runtime/mem.h"

#define ARENA_BYTES (16u * 1024 * 1024)
#define EXE "original/stars26i.exe"
#define CARGOBMP_BIN "re/resources/0021-bitmap-CARGOBMP.bin"

#define WHITE_IDX 255
#define BLACK_IDX 0
#define RED_IDX 249
#define BLUE_IDX 252
#define SILVER_IDX 7
#define GRAY_IDX 248

static HDC16 g_scr;

static const COLORREF16 C_BLACK = GDI_RGB(0, 0, 0);
static const COLORREF16 C_WHITE = GDI_RGB(255, 255, 255);
static const COLORREF16 C_RED = GDI_RGB(255, 0, 0);
static const COLORREF16 C_BLUE = GDI_RGB(0, 0, 255);

static int legend_index(char c) {
  switch (c) {
    case '.': return WHITE_IDX;
    case '#': return BLACK_IDX;
    case 'r': return RED_IDX;
    case 'b': return BLUE_IDX;
    case 's': return SILVER_IDX;
    case 'g': return GRAY_IDX;
    default: return -1;
  }
}

/* A w x h colour memory DC filled white. */
static HDC16 mk_dc(int w, int h, HBITMAP16 *hbm_out) {
  HDC16 dc = CreateCompatibleDC(g_scr);
  HBITMAP16 bm = CreateCompatibleBitmap(g_scr, (int16_t)w, (int16_t)h);
  CHECK(dc != 0);
  CHECK(bm != 0);
  SelectObject(dc, bm);
  CHECK_EQ(PatBlt(dc, 0, 0, (int16_t)w, (int16_t)h, WHITENESS), 1);
  if (hbm_out) *hbm_out = bm;
  return dc;
}

static void free_dc(HDC16 dc, HBITMAP16 bm) {
  /* Select the default bitmap back by creating a throw-away DC's: simply delete the DC, which
   * releases the selection, then the bitmap. */
  CHECK_EQ(DeleteDC(dc), 1);
  CHECK_EQ(DeleteObject(bm), 1);
}

static int pix(HBITMAP16 bm, int x, int y) {
  int w, h;
  bool mono;
  uint8_t *p = gdi_bitmap_bits(bm, &w, &h, &mono);
  if (!p || x < 0 || y < 0 || x >= w || y >= h) return -1;
  return p[y * w + x];
}

static void check_grid_at(const char *file, int line, HBITMAP16 bm, int w, int h, const char *const *rows) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      int want = legend_index(rows[y][x]);
      int got = pix(bm, x, y);
      test_checks_run++;
      if (got != want) {
        test_checks_failed++;
        fprintf(stderr, "%s:%d: pixel (%d,%d) got %d, want %d ('%c')\n", file, line, x, y, got, want,
                rows[y][x]);
      }
    }
}
#define CHECK_GRID(bm, w, h, rows) check_grid_at(__FILE__, __LINE__, (bm), (w), (h), (rows))

static HBRUSH16 solid(COLORREF16 c) {
  HBRUSH16 b = CreateSolidBrush(c);
  CHECK(b != 0);
  return b;
}

/* ---------------------------------------------------------------- raster operations */

static uint32_t g_seed = 12345;
static uint8_t rnd8(void) {
  g_seed = g_seed * 1664525u + 1013904223u;
  return (uint8_t)(g_seed >> 24);
}

/* Naive truth-table reference: bit by bit, index = (P << 2) | (S << 1) | D. */
static uint8_t rop3_ref(uint8_t table, uint8_t p, uint8_t s, uint8_t d) {
  uint8_t r = 0;
  for (int b = 0; b < 8; b++) {
    int k = (((p >> b) & 1) << 2) | (((s >> b) & 1) << 1) | ((d >> b) & 1);
    r |= (uint8_t)(((table >> k) & 1) << b);
  }
  return r;
}

static void test_rop3_all_256(void) {
  /* Random 8x8 P, S and D bit patterns (64 bytes each) for every one of the 256 codes. */
  uint8_t P[64], S[64], D[64];
  for (int i = 0; i < 64; i++) { P[i] = rnd8(); S[i] = rnd8(); D[i] = rnd8(); }
  int bad = 0;
  for (int t = 0; t < 256; t++) {
    /* The low word of the code is the Microsoft operation number; it must not matter. */
    uint32_t codes[2] = {(uint32_t)t << 16, ((uint32_t)t << 16) | 0xFFFFu};
    for (int c = 0; c < 2; c++)
      for (int i = 0; i < 64; i++)
        if (gdi_rop3(codes[c], P[i], S[i], D[i]) != rop3_ref((uint8_t)t, P[i], S[i], D[i])) bad++;
  }
  CHECK_EQ(bad, 0);
  /* A few named codes against their algebra. */
  for (int i = 0; i < 64; i++) {
    CHECK_EQ(gdi_rop3(SRCCOPY, P[i], S[i], D[i]), S[i]);
    CHECK_EQ(gdi_rop3(PATCOPY, P[i], S[i], D[i]), P[i]);
    CHECK_EQ(gdi_rop3(PATINVERT, P[i], S[i], D[i]), (uint8_t)(P[i] ^ D[i]));
    CHECK_EQ(gdi_rop3(SRCINVERT, P[i], S[i], D[i]), (uint8_t)(S[i] ^ D[i]));
    CHECK_EQ(gdi_rop3(SRCAND, P[i], S[i], D[i]), (uint8_t)(S[i] & D[i]));
    CHECK_EQ(gdi_rop3(SRCPAINT, P[i], S[i], D[i]), (uint8_t)(S[i] | D[i]));
    CHECK_EQ(gdi_rop3(DSTINVERT, P[i], S[i], D[i]), (uint8_t)~D[i]);
    CHECK_EQ(gdi_rop3(NOTSRCCOPY, P[i], S[i], D[i]), (uint8_t)~S[i]);
    CHECK_EQ(gdi_rop3(BLACKNESS, P[i], S[i], D[i]), 0);
    CHECK_EQ(gdi_rop3(WHITENESS, P[i], S[i], D[i]), 0xFF);
    CHECK_EQ(gdi_rop3(MERGECOPY, P[i], S[i], D[i]), (uint8_t)(P[i] & S[i]));
    CHECK_EQ(gdi_rop3(MERGEPAINT, P[i], S[i], D[i]), (uint8_t)(~S[i] | D[i]));
    CHECK_EQ(gdi_rop3(NOTSRCERASE, P[i], S[i], D[i]), (uint8_t)~(S[i] | D[i]));
    CHECK_EQ(gdi_rop3(SRCERASE, P[i], S[i], D[i]), (uint8_t)(S[i] & ~D[i]));
    CHECK_EQ(gdi_rop3(PATPAINT, P[i], S[i], D[i]), (uint8_t)(P[i] | ~S[i] | D[i]));
  }
}

static uint8_t rop2_expr(int n, uint8_t p, uint8_t d) {
  switch (n) {
    case R2_BLACK: return 0;
    case R2_NOTMERGEPEN: return (uint8_t)~(d | p);
    case R2_MASKNOTPEN: return (uint8_t)(~p & d);
    case R2_NOTCOPYPEN: return (uint8_t)~p;
    case R2_MASKPENNOT: return (uint8_t)(p & ~d);
    case R2_NOT: return (uint8_t)~d;
    case R2_XORPEN: return (uint8_t)(p ^ d);
    case R2_NOTMASKPEN: return (uint8_t)~(p & d);
    case R2_MASKPEN: return (uint8_t)(p & d);
    case R2_NOTXORPEN: return (uint8_t)~(p ^ d);
    case R2_NOP: return d;
    case R2_MERGENOTPEN: return (uint8_t)(~p | d);
    case R2_COPYPEN: return p;
    case R2_MERGEPENNOT: return (uint8_t)(p | ~d);
    case R2_MERGEPEN: return (uint8_t)(p | d);
    case R2_WHITE: return 0xFF;
  }
  return 0;
}

static void test_rop2_all_16(void) {
  int bad = 0;
  for (int n = 1; n <= 16; n++)
    for (int p = 0; p < 256; p += 3)
      for (int d = 0; d < 256; d += 5)
        if (gdi_rop2(n, (uint8_t)p, (uint8_t)d) != rop2_expr(n, (uint8_t)p, (uint8_t)d)) bad++;
  CHECK_EQ(bad, 0);
}

/* ---------------------------------------------------------------- primitives */

static void test_lineto_end_exclusive(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  /* Default pen is BLACK_PEN, 1 px. LineTo (0,0)->(4,0): x = 0..3 black, x = 4 white. */
  CHECK_EQ(MoveTo(dc, 0, 0), 0);          /* old position (0,0) packs to 0 */
  CHECK_EQ(LineTo(dc, 4, 0), 1);
  static const char *const rows[8] = {"####....", "........", "........", "........",
                                      "........", "........", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows);
  /* The current position is the line end, so the next LineTo continues from (4,0). */
  CHECK_EQ(MoveTo(dc, 0, 7), (uint32_t)(4 | (0 << 16)));
  free_dc(dc, bm);
}

static void test_lineto_diagonal_vertical(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  MoveTo(dc, 0, 0);
  LineTo(dc, 4, 4);
  MoveTo(dc, 7, 0);
  LineTo(dc, 7, 3);
  static const char *const rows[8] = {"#......#", ".#.....#", "..#....#", "...#....",
                                      "........", "........", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows);
  free_dc(dc, bm);
}

static void test_rectangle_outline_only(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  SelectObject(dc, GetStockObject(NULL_BRUSH));
  CHECK_EQ(Rectangle(dc, 1, 1, 5, 5), 1);
  /* Outline at x,y in 1..4 only; the interior keeps the background. */
  static const char *const rows[8] = {"........", ".####...", ".#..#...", ".#..#...",
                                      ".####...", "........", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows);
  free_dc(dc, bm);
}

static void test_rectangle_pen_and_brush(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  HBRUSH16 red = solid(C_RED);
  SelectObject(dc, red);
  Rectangle(dc, 1, 1, 6, 5);
  static const char *const rows[8] = {"........", ".#####..", ".#rrr#..", ".#rrr#..",
                                      ".#####..", "........", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows);
  /* A NULL pen fills the whole rectangle up to (right-1, bottom-1) with the brush.
   * UNVERIFIED: this is the runtime's current rule, not a Windows-verified one. Windows may
   * instead fill [l, r-1) x [t, b-1) (one column and row fewer). Needs a reference capture
   * (docs/inventory.md section 11); do not "fix" the expectation without one. */
  PatBlt(dc, 0, 0, 8, 8, WHITENESS);
  HGDIOBJ16 oldpen = SelectObject(dc, GetStockObject(NULL_PEN));
  CHECK_EQ(oldpen, GetStockObject(BLACK_PEN));
  Rectangle(dc, 1, 1, 4, 3);
  static const char *const rows2[8] = {"........", ".rrr....", ".rrr....", "........",
                                       "........", "........", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows2);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(red), 1);
}

static void test_ellipse(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  SelectObject(dc, GetStockObject(NULL_BRUSH));
  Ellipse(dc, 0, 0, 8, 8);
  /* By definition (docs/inventory.md): a pixel is inside when its centre is inside the
   * bounding-box ellipse; the outline is the inside pixels with a 4-neighbour outside.
   * Not verified against Windows (no reference capture yet). */
  static const char *const rows[8] = {"..####..", ".#....#.", "#......#", "#......#",
                                      "#......#", "#......#", ".#....#.", "..####.."};
  CHECK_GRID(bm, 8, 8, rows);
  free_dc(dc, bm);
}

static void test_patinvert_twice(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  MoveTo(dc, 0, 0);
  LineTo(dc, 6, 6);
  uint8_t before[64];
  memcpy(before, gdi_bitmap_bits(bm, NULL, NULL, NULL), 64);
  HBRUSH16 red = solid(C_RED);
  SelectObject(dc, red);
  PatBlt(dc, 1, 1, 6, 6, PATINVERT);
  /* Changed: a white pixel xor 249 = 6, a black one = 249. */
  CHECK_EQ(pix(bm, 1, 2), WHITE_IDX ^ RED_IDX);
  CHECK_EQ(pix(bm, 1, 1), BLACK_IDX ^ RED_IDX);
  CHECK_EQ(pix(bm, 0, 0), BLACK_IDX); /* outside the rectangle */
  PatBlt(dc, 1, 1, 6, 6, PATINVERT);
  CHECK_MEM(gdi_bitmap_bits(bm, NULL, NULL, NULL), before, 64);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(red), 1);
}

static void test_rop2_xor_line_twice(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  HPEN16 pen = CreatePen(PS_SOLID, 1, C_RED);
  SelectObject(dc, pen);
  uint8_t before[64];
  memcpy(before, gdi_bitmap_bits(bm, NULL, NULL, NULL), 64);
  CHECK_EQ(GetROP2(dc), R2_COPYPEN);
  CHECK_EQ(SetROP2(dc, R2_XORPEN), R2_COPYPEN);
  CHECK_EQ(GetROP2(dc), R2_XORPEN);
  MoveTo(dc, 0, 3);
  LineTo(dc, 8, 3);
  CHECK_EQ(pix(bm, 4, 3), WHITE_IDX ^ RED_IDX);
  CHECK_EQ(pix(bm, 4, 2), WHITE_IDX);
  MoveTo(dc, 0, 3);
  LineTo(dc, 8, 3);
  CHECK_MEM(gdi_bitmap_bits(bm, NULL, NULL, NULL), before, 64);
  SelectObject(dc, GetStockObject(BLACK_PEN));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(pen), 1);
}

static void test_dashed_pens_and_transparent(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 2, &bm);
  HPEN16 dot = CreatePen(PS_DOT, 1, C_BLACK);
  SelectObject(dc, dot);
  SetBkColor(dc, C_BLUE);
  MoveTo(dc, 0, 0);
  LineTo(dc, 16, 0);                 /* OPAQUE: the gaps take the background colour */
  SetBkMode(dc, TRANSPARENT);
  MoveTo(dc, 0, 1);
  LineTo(dc, 16, 1);                 /* TRANSPARENT: the gaps are untouched */
  int blue = 0, black = 0, white = 0;
  for (int x = 0; x < 16; x++) {
    if (pix(bm, x, 0) == BLUE_IDX) blue++;
    if (pix(bm, x, 0) == BLACK_IDX) black++;
    if (pix(bm, x, 1) == WHITE_IDX) white++;
  }
  CHECK(blue > 0);
  CHECK(black > 0);
  CHECK_EQ(blue + black, 16);
  CHECK_EQ(white, blue);
  SelectObject(dc, GetStockObject(BLACK_PEN));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(dot), 1);
}

/* UNVERIFIED (item 5b): a Windows PS_SOLID pen wider than 1 is centred on the edge; this runtime
 * stamps solid squares. The game uses width 1 only (docs/inventory.md section 11). */
static void test_wide_pen_xor_no_double_hit(void) {
  /* A 3 px XOR line must flip each pixel once, even where successive stamps overlap. */
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 16, &bm);
  HPEN16 pen = CreatePen(PS_SOLID, 3, C_RED);
  SelectObject(dc, pen);
  SetROP2(dc, R2_XORPEN);
  MoveTo(dc, 4, 8);
  LineTo(dc, 12, 8);
  for (int y = 7; y <= 9; y++) CHECK_EQ(pix(bm, 6, y), WHITE_IDX ^ RED_IDX);
  CHECK_EQ(pix(bm, 6, 6), WHITE_IDX);
  CHECK_EQ(pix(bm, 6, 10), WHITE_IDX);
  SelectObject(dc, GetStockObject(BLACK_PEN));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(pen), 1);
}

/* ---------------------------------------------------------------- blits */

static void test_mono_to_colour_bitblt(void) {
  /* A monochrome memory bitmap: CreateCompatibleBitmap on a fresh memory DC (which holds the
   * 1x1 monochrome default bitmap) is monochrome. */
  HDC16 src = CreateCompatibleDC(g_scr);
  HBITMAP16 mono = CreateCompatibleBitmap(src, 8, 8);
  bool is_mono = false;
  int w, h;
  CHECK(gdi_bitmap_bits(mono, &w, &h, &is_mono) != NULL);
  CHECK(is_mono);
  SelectObject(src, mono);
  /* Bit pattern: row 0 = 0 1 0 1 0 1 0 1, other rows 0. SetPixel on a mono DC: black -> 0,
   * white -> 1. */
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++)
      SetPixel(src, (int16_t)x, (int16_t)y, (y == 0 && (x & 1)) ? C_WHITE : C_BLACK);
  CHECK_EQ(pix(mono, 0, 0), 0);
  CHECK_EQ(pix(mono, 1, 0), 1);

  HBITMAP16 bm;
  HDC16 dst = mk_dc(8, 8, &bm);
  SetTextColor(dst, C_RED);
  SetBkColor(dst, C_BLUE);
  CHECK_EQ(BitBlt(dst, 0, 0, 8, 8, src, 0, 0, SRCCOPY), 1);
  /* Windows 3.1: 0 bits take the text colour, 1 bits the background colour. */
  static const char *const rows[8] = {"rbrbrbrb", "rrrrrrrr", "rrrrrrrr", "rrrrrrrr",
                                      "rrrrrrrr", "rrrrrrrr", "rrrrrrrr", "rrrrrrrr"};
  CHECK_GRID(bm, 8, 8, rows);
  free_dc(dst, bm);
  CHECK_EQ(DeleteDC(src), 1);
  CHECK_EQ(DeleteObject(mono), 1);
}

static void test_bitblt_colour_overlap_and_rops(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  MoveTo(dc, 0, 0);
  LineTo(dc, 8, 0);                   /* row 0 black */
  /* Overlapping copy inside one surface (down by one row): behaves as a memmove. */
  CHECK_EQ(BitBlt(dc, 0, 1, 8, 7, dc, 0, 0, SRCCOPY), 1);
  CHECK_EQ(pix(bm, 3, 0), BLACK_IDX);
  CHECK_EQ(pix(bm, 3, 1), BLACK_IDX);
  CHECK_EQ(pix(bm, 3, 2), WHITE_IDX);  /* row 1 was white before; a naive forward copy smears */
  /* SRCINVERT with itself clears to zero. */
  CHECK_EQ(BitBlt(dc, 0, 0, 4, 4, dc, 0, 0, SRCINVERT), 1);
  CHECK_EQ(pix(bm, 1, 1), 0);
  /* A source ROP with no source DC fails; a pattern-only one succeeds. */
  CHECK_EQ(BitBlt(dc, 0, 0, 2, 2, 0, 0, 0, SRCCOPY), 0);
  CHECK_EQ(BitBlt(dc, 0, 0, 2, 2, 0, 0, 0, WHITENESS), 1);
  free_dc(dc, bm);
}

/* ---------------------------------------------------------------- clipping */

static void test_exclude_clip_rect_hole(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  HBRUSH16 blk = solid(C_BLACK);
  SelectObject(dc, blk);
  CHECK_EQ(ExcludeClipRect(dc, 2, 2, 4, 4), COMPLEXREGION);
  PatBlt(dc, 0, 0, 8, 8, PATCOPY);   /* FillRect of the whole 8x8 */
  static const char *const rows[8] = {"########", "########", "##..####", "##..####",
                                      "########", "########", "########", "########"};
  CHECK_GRID(bm, 8, 8, rows);
  /* Drawing primitives are clipped too. */
  PatBlt(dc, 0, 0, 8, 8, WHITENESS);
  MoveTo(dc, 0, 2);
  LineTo(dc, 8, 2);
  CHECK_EQ(pix(bm, 1, 2), BLACK_IDX);
  CHECK_EQ(pix(bm, 2, 2), WHITE_IDX);
  CHECK_EQ(pix(bm, 3, 2), WHITE_IDX);
  CHECK_EQ(pix(bm, 4, 2), BLACK_IDX);
  /* Removing the clip restores drawing everywhere. */
  CHECK_EQ(SelectClipRgn(dc, 0), SIMPLEREGION);
  PatBlt(dc, 0, 0, 8, 8, PATCOPY);
  CHECK_EQ(pix(bm, 2, 2), BLACK_IDX);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(blk), 1);
}

static void test_intersect_and_region_clip(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  CHECK_EQ(IntersectClipRect(dc, 2, 2, 6, 6), SIMPLEREGION);
  CHECK_EQ(IntersectClipRect(dc, 4, 4, 9, 9), SIMPLEREGION);   /* now (4,4)-(6,6) */
  CHECK_EQ(IntersectClipRect(dc, 0, 0, 3, 3), NULLREGION);     /* disjoint */
  PatBlt(dc, 0, 0, 8, 8, BLACKNESS);
  int black = 0;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) black += pix(bm, x, y) == BLACK_IDX;
  CHECK_EQ(black, 0);
  HRGN16 rgn = CreateRectRgn(2, 2, 6, 6);
  CHECK(rgn != 0);
  CHECK_EQ(SelectClipRgn(dc, rgn), SIMPLEREGION);
  PatBlt(dc, 0, 0, 8, 8, BLACKNESS);
  static const char *const rows[8] = {"........", "........", "..####..", "..####..",
                                      "..####..", "..####..", "........", "........"};
  CHECK_GRID(bm, 8, 8, rows);
  CHECK_EQ(SelectClipRgn(dc, 0), SIMPLEREGION);
  CHECK_EQ(DeleteObject(rgn), 1);   /* SelectClipRgn copied the region */
  free_dc(dc, bm);
}

static void test_window_org_and_device_origin(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  CHECK_EQ(SetWindowOrg(dc, 2, 3), 0);
  MoveTo(dc, 2, 3);
  SetPixel(dc, 2, 3, C_BLACK);          /* logical (2,3) is device (0,0) */
  CHECK_EQ(pix(bm, 0, 0), BLACK_IDX);
  CHECK_EQ(SetWindowOrg(dc, 0, 0), (uint32_t)(2 | (3 << 16)));
  free_dc(dc, bm);

  /* The screen DC: device origin and hard clip belong to USER's DCs. */
  HDC16 s = gdi_create_screen_dc();
  PatBlt(s, 0, 0, 800, 600, WHITENESS);
  gdi_dc_set_origin(s, 100, 50);
  gdi_dc_set_hard_clip(s, 100, 50, 104, 54);
  PatBlt(s, 0, 0, 10, 10, BLACKNESS);
  const uint8_t *fbp = fb_pixels();
  CHECK_EQ(fbp[50 * GDI_SCREEN_W + 100], BLACK_IDX);
  CHECK_EQ(fbp[53 * GDI_SCREEN_W + 103], BLACK_IDX);
  CHECK_EQ(fbp[50 * GDI_SCREEN_W + 104], WHITE_IDX);   /* beyond the hard clip */
  CHECK_EQ(fbp[54 * GDI_SCREEN_W + 100], WHITE_IDX);
  CHECK_EQ(fbp[50 * GDI_SCREEN_W + 99], WHITE_IDX);
  CHECK_EQ(DeleteDC(s), 1);
}

/* ---------------------------------------------------------------- DIB */

/* The CARGOBMP resource of stars26i.exe: BITMAPINFOHEADER (40) + 16 RGBQUADs + 8 rows of
 * 4 bytes (4 bpp, bottom-up). Rebuilt here from its documented layout so the test needs no
 * game file; test_cargobmp_matches_resource cross-checks it against re/resources. */
static void build_cargo_dib(uint8_t *b /* 40 + 64 + 32 = 136 bytes */) {
  /* The resource's own table: dark entries are c0, not 80 (read from the file). */
  static const uint8_t vga[16][3] = {{0, 0, 0},       {192, 0, 0},   {0, 192, 0},   {192, 192, 0},
                                     {0, 0, 192},     {192, 0, 192}, {0, 192, 192}, {192, 192, 192},
                                     {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                     {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
  memset(b, 0, 136);
  b[0] = 40; b[4] = 8; b[8] = 8; b[12] = 1; b[14] = 4; b[20] = 32; /* biSizeImage; biClrUsed stays 0 as in the resource */
  for (int i = 0; i < 16; i++) { b[40 + i * 4] = vga[i][2]; b[41 + i * 4] = vga[i][1]; b[42 + i * 4] = vga[i][0]; }
  /* File rows 0..7 (bottom first): even rows "8 7 7 7 f 7 7 7", odd rows "7 8 7 7 7 f 7 7". */
  static const uint8_t even[4] = {0x87, 0x77, 0xf7, 0x77};
  static const uint8_t odd[4] = {0x78, 0x77, 0x7f, 0x77};
  for (int r = 0; r < 8; r++) memcpy(b + 104 + r * 4, (r & 1) ? odd : even, 4);
}

static FARPTR far_alloc(uint32_t n, uint16_t *hout) {
  uint16_t h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, n);
  CHECK(h != 0);
  if (hout) *hout = h;
  return GlobalLock(h);
}

static void test_stretchdibits_cargo_1to1(void) {
  uint8_t dib[136];
  build_cargo_dib(dib);
  uint16_t hg;
  FARPTR fp = far_alloc(136, &hg);
  memcpy(LIN(fp), dib, 136);
  FARPTR bmi = fp, bits = fp + 104;

  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  CHECK_EQ(StretchDIBits(dc, 0, 0, 8, 8, 0, 0, 8, 8, bits, bmi, DIB_RGB_COLORS, SRCCOPY), 8);
  /* DIB nibble 7 = c0c0c0 -> system index 7, 8 = 808080 -> 248, f = ffffff -> 255.
   * The bottom-up DIB shows its last stored row (odd) first. */
  static const char *const want[8] = {"sgsss.ss", "gsss.sss", "sgsss.ss", "gsss.sss",
                                      "sgsss.ss", "gsss.sss", "sgsss.ss", "gsss.sss"};
  /* Row 0 is file row 7 (odd): nibbles 7 8 7 7 7 f 7 7 -> s g s s s . s s.
   * Row 1 is file row 6 (even): 8 7 7 7 f 7 7 7 -> g s s s . s s s. */
  CHECK_GRID(bm, 8, 8, want);

  /* 2x enlargement replicates pixels: (x,y) of the source covers (2x..2x+1, 2y..2y+1). */
  HBITMAP16 bm2;
  HDC16 dc2 = mk_dc(16, 16, &bm2);
  CHECK_EQ(StretchDIBits(dc2, 0, 0, 16, 16, 0, 0, 8, 8, bits, bmi, DIB_RGB_COLORS, SRCCOPY), 8);
  CHECK_EQ(pix(bm2, 2, 0), GRAY_IDX);
  CHECK_EQ(pix(bm2, 3, 1), GRAY_IDX);
  CHECK_EQ(pix(bm2, 4, 0), SILVER_IDX);
  CHECK_EQ(pix(bm2, 0, 2), GRAY_IDX);
  CHECK_EQ(pix(bm2, 10, 0), WHITE_IDX);
  free_dc(dc2, bm2);

  /* Unsupported compression fails with 0. */
  uint8_t *p = LIN(fp);
  p[16] = 1;
  CHECK_EQ(StretchDIBits(dc, 0, 0, 8, 8, 0, 0, 8, 8, bits, bmi, DIB_RGB_COLORS, SRCCOPY), 0);
  p[16] = 0;
  free_dc(dc, bm);
  GlobalUnlock(hg);
  GlobalFree(hg);
}

static void test_cargobmp_matches_resource(void) {
  FILE *f = fopen(CARGOBMP_BIN, "rb");
  if (!f) return;   /* local data (re/ is git-ignored): skip when absent */
  uint8_t res[192], dib[136];
  size_t n = fread(res, 1, sizeof res, f);
  fclose(f);
  build_cargo_dib(dib);
  CHECK_EQ(n, 192);
  CHECK_MEM(res, dib, 136);
}

static void test_getdibits_roundtrip(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  HBRUSH16 red = solid(C_RED);
  SelectObject(dc, red);
  PatBlt(dc, 0, 0, 8, 4, PATCOPY);          /* top half red, bottom half white */
  uint16_t hg;
  FARPTR fp = far_alloc(40 + 1024 + 64, &hg);
  uint8_t *p = LIN(fp);
  /* Header-only call (lpBits NULL) fills the header from the bitmap. */
  p[0] = 40;
  CHECK_EQ(GetDIBits(dc, bm, 0, 8, 0, fp, DIB_RGB_COLORS), 1);
  CHECK_EQ(p[4], 8);                         /* biWidth */
  CHECK_EQ(p[8], 8);                         /* biHeight */
  CHECK_EQ(p[14], 8);                        /* biBitCount */
  /* Bits: bottom-up 8 bpp, the colour table holds the system palette. */
  CHECK_EQ(GetDIBits(dc, bm, 0, 8, fp + 40 + 1024, fp, DIB_RGB_COLORS), 8);
  const uint8_t *bits = p + 40 + 1024;
  CHECK_EQ(bits[0 * 8 + 0], WHITE_IDX);      /* file row 0 = bottom scan line = white */
  CHECK_EQ(bits[7 * 8 + 0], RED_IDX);        /* last file row = top = red */
  const uint8_t *tab = p + 40;               /* RGBQUAD B,G,R,0 */
  CHECK_EQ(tab[RED_IDX * 4 + 2], 255);
  CHECK_EQ(tab[RED_IDX * 4 + 1], 0);
  CHECK_EQ(tab[RED_IDX * 4 + 0], 0);
  GlobalUnlock(hg);
  GlobalFree(hg);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(red), 1);
}

/* ---------------------------------------------------------------- brushes, palette, objects */

static void test_dither_and_exact_brush(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 16, &bm);
  /* Exact system colour: solid. */
  HBRUSH16 bsil = solid(GDI_RGB(192, 192, 192));
  SelectObject(dc, bsil);
  PatBlt(dc, 0, 0, 16, 16, PATCOPY);
  int all = 1;
  for (int i = 0; i < 256; i++) all &= pix(bm, i % 16, i / 16) == SILVER_IDX;
  CHECK(all);
  /* A colour that is not in the palette is dithered by the driver from the static colours, in
   * an 8x8 pattern. Unverified against captures: only structure is asserted (8-periodic,
   * static colours only, more than one colour, deterministic mean close to the request). */
  HBRUSH16 bdit = solid(GDI_RGB(100, 100, 100));
  SelectObject(dc, bdit);
  PatBlt(dc, 0, 0, 16, 16, PATCOPY);
  int periodic = 1, statics = 1, distinct_seen[256] = {0}, ndistinct = 0;
  long sr = 0, sg = 0, sb = 0;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) {
      int v = pix(bm, x, y);
      if (v != pix(bm, x & 7, y & 7)) periodic = 0;
      if (!(v < 10 || v >= 246)) statics = 0;
      if (!distinct_seen[v]) { distinct_seen[v] = 1; ndistinct++; }
      COLORREF16 c = fb_palette_entry((unsigned)v);
      sr += c & 255; sg += (c >> 8) & 255; sb += (c >> 16) & 255;
    }
  CHECK(periodic);
  CHECK(statics);
  CHECK(ndistinct >= 2);
  CHECK(labs(sr / 256 - 100) <= 8);
  CHECK(labs(sg / 256 - 100) <= 8);
  CHECK(labs(sb / 256 - 100) <= 8);
  /* The brush origin shifts the pattern. */
  uint8_t keep[64];
  for (int i = 0; i < 64; i++) keep[i] = (uint8_t)pix(bm, i % 8, i / 8);
  CHECK_EQ(SetBrushOrg(dc, 1, 0), 0);
  PatBlt(dc, 0, 0, 16, 16, PATCOPY);
  int same = 1;
  for (int i = 0; i < 64; i++) same &= pix(bm, (i % 8) + 1, i / 8) == keep[i];
  CHECK(same);
  /* Pens and SetPixel use the nearest colour instead. */
  CHECK_EQ(SetPixel(dc, 0, 0, GDI_RGB(100, 100, 100)) != 0xFFFFFFFFu, 1);
  CHECK(pix(bm, 0, 0) < 10 || pix(bm, 0, 0) >= 246);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(bsil), 1);
  CHECK_EQ(DeleteObject(bdit), 1);
}

static void test_pattern_brush(void) {
  /* A monochrome 8x8 bitmap becomes a brush: 0 bits text colour, 1 bits background colour. */
  HDC16 src = CreateCompatibleDC(g_scr);
  HBITMAP16 mono = CreateCompatibleBitmap(src, 8, 8);
  SelectObject(src, mono);
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) SetPixel(src, (int16_t)x, (int16_t)y, ((x + y) & 1) ? C_WHITE : C_BLACK);
  HBRUSH16 pb = CreatePatternBrush(mono);
  CHECK(pb != 0);
  CHECK_EQ(DeleteDC(src), 1);
  CHECK_EQ(DeleteObject(mono), 1);   /* the brush owns a copy */
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  SetTextColor(dc, C_RED);
  SetBkColor(dc, C_BLUE);
  SelectObject(dc, pb);
  PatBlt(dc, 0, 0, 8, 8, PATCOPY);
  CHECK_EQ(pix(bm, 0, 0), RED_IDX);
  CHECK_EQ(pix(bm, 1, 0), BLUE_IDX);
  CHECK_EQ(pix(bm, 0, 1), BLUE_IDX);
  CHECK_EQ(pix(bm, 1, 1), RED_IDX);
  CHECK_EQ(UnrealizeObject(pb), 1);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  free_dc(dc, bm);
  CHECK_EQ(DeleteObject(pb), 1);
}

static void test_palette(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  /* LOGPALETTE: version 0x300, 3 entries {R,G,B,flags}: red (already a static colour), and two
   * colours that are not in the system palette. */
  uint16_t hg;
  FARPTR fp = far_alloc(4 + 3 * 4, &hg);
  uint8_t *p = LIN(fp);
  p[0] = 0x00; p[1] = 0x03; p[2] = 3; p[3] = 0;
  static const uint8_t ent[3][4] = {{255, 0, 0, 0}, {10, 20, 30, 0}, {200, 150, 100, 0}};
  memcpy(p + 4, ent, 12);
  HPALETTE16 pal = CreatePalette(fp);
  CHECK(pal != 0);
  CHECK(pal != GetStockObject(DEFAULT_PALETTE));
  HPALETTE16 old = SelectPalette(dc, pal, 0);
  CHECK_EQ(old, GetStockObject(DEFAULT_PALETTE));
  CHECK_EQ(DeleteObject(pal), 0);            /* selected into a DC */
  /* Returns the number of logical entries mapped to a different system entry. */
  CHECK_EQ(RealizePalette(dc), 3);
  CHECK_EQ(RealizePalette(dc), 0);           /* nothing changes the second time */
  /* Entry 0 reuses the static red; entries 1-2 take free slots 10..245 holding that colour. */
  HPEN16 p0 = CreatePen(PS_SOLID, 1, GDI_PALETTEINDEX(0));
  HPEN16 p1 = CreatePen(PS_SOLID, 1, GDI_PALETTEINDEX(1));
  HPEN16 p2 = CreatePen(PS_SOLID, 1, GDI_PALETTERGB(200, 150, 100));
  SelectObject(dc, p0); MoveTo(dc, 0, 0); LineTo(dc, 2, 0);
  SelectObject(dc, p1); MoveTo(dc, 0, 1); LineTo(dc, 2, 1);
  SelectObject(dc, p2); MoveTo(dc, 0, 2); LineTo(dc, 2, 2);
  CHECK_EQ(pix(bm, 0, 0), RED_IDX);
  int i1 = pix(bm, 0, 1), i2 = pix(bm, 0, 2);
  CHECK(i1 >= 10 && i1 <= 245);
  CHECK(i2 >= 10 && i2 <= 245);
  CHECK(i1 != i2);
  CHECK_EQ(fb_palette_entry((unsigned)i1), GDI_RGB(10, 20, 30));
  CHECK_EQ(fb_palette_entry((unsigned)i2), GDI_RGB(200, 150, 100));
  /* A plain RGB request matches only the 20 STATIC colours, never the realized entries
   * (Windows: explicit RGB pens, text and SetPixel go to the nearest static colour; only
   * PALETTERGB / PALETTEINDEX / DIB colours use the realized palette). (10,20,30) has an exact
   * realized entry i1 but the nearest static is black. From the Windows 3.1 palette
   * documentation; no reference capture yet. */
  CHECK_EQ(SetPixel(dc, 7, 7, GDI_RGB(10, 20, 30)), (uint32_t)GDI_RGB(0, 0, 0));
  CHECK_EQ(pix(bm, 7, 7), BLACK_IDX);
  /* (200,150,100) is realized exactly at i2, yet the nearest static is 160,160,164 (247). */
  CHECK_EQ(SetPixel(dc, 6, 7, GDI_RGB(200, 150, 100)), (uint32_t)GDI_RGB(160, 160, 164));
  CHECK_EQ(pix(bm, 6, 7), 247);
  CHECK(pix(bm, 6, 7) != i2);
  /* A solid brush for a plain RGB colour is built from the statics (exact or dither), so it
   * never uses the realized slot i1 either. */
  HBRUSH16 sb = solid(GDI_RGB(10, 20, 30));
  HGDIOBJ16 oldb = SelectObject(dc, sb);
  PatBlt(dc, 0, 0, 8, 8, PATCOPY);
  int only_static = 1;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      int v = pix(bm, x, y);
      if (!(v < 10 || v >= 246)) only_static = 0;
    }
  CHECK_EQ(only_static, 1);
  SelectObject(dc, oldb);
  CHECK_EQ(DeleteObject(sb), 1);
  /* A PALETTERGB request still uses the realized entries: exact colour -> its own slot. */
  CHECK_EQ(SetPixel(dc, 5, 7, GDI_PALETTERGB(10, 20, 30)), (uint32_t)GDI_RGB(10, 20, 30));
  CHECK_EQ(pix(bm, 5, 7), i1);
  SelectObject(dc, GetStockObject(BLACK_PEN));
  CHECK_EQ(SelectPalette(dc, GetStockObject(DEFAULT_PALETTE), 0), pal);
  CHECK_EQ(DeleteObject(pal), 1);
  CHECK_EQ(DeleteObject(p0), 1);
  CHECK_EQ(DeleteObject(p1), 1);
  CHECK_EQ(DeleteObject(p2), 1);
  GlobalUnlock(hg);
  GlobalFree(hg);
  free_dc(dc, bm);
}

/* Item 2 (stars-0z4.4): PC_RESERVED entries take unused system slots, never collapse onto an
 * existing colour, and other colours never match them. The game builds a 256-entry LOGPALETTE
 * of {0,0,0,PC_RESERVED} at 1038:a9d2. NEEDS A REFERENCE CAPTURE of the real game palette and of
 * what Windows does when the 236 free slots run out (here: the surplus 20 entries map to the
 * nearest non-reserved colour, black); the count 256 follows from "first realize changes
 * every entry". */
static void test_palette_reserved(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  uint16_t hg;
  FARPTR fp = far_alloc(4 + 256 * 4, &hg);
  uint8_t *p = LIN(fp);
  p[0] = 0x00; p[1] = 0x03; p[2] = 0; p[3] = 1;   /* 256 entries */
  for (int i = 0; i < 256; i++) p[4 + 4 * i + 3] = PC_RESERVED;   /* {0,0,0,PC_RESERVED} */
  HPALETTE16 pal = CreatePalette(fp);
  CHECK(pal != 0);
  HPALETTE16 oldpal = SelectPalette(dc, pal, 0);
  CHECK_EQ(RealizePalette(dc), 256);
  CHECK_EQ(RealizePalette(dc), 0);
  /* No entry that got a slot collapsed onto black (index 0): the first 236 take distinct
   * free slots 10..245 (checked through PALETTEINDEX pens). */
  int seen[256] = {0}, collapsed = 0, in_range = 0;
  for (int i = 0; i < 236; i++) {
    HPEN16 pn = CreatePen(PS_SOLID, 1, GDI_PALETTEINDEX(i));
    HGDIOBJ16 op = SelectObject(dc, pn);
    PatBlt(dc, 0, 0, 8, 8, WHITENESS);
    MoveTo(dc, 0, 0);
    LineTo(dc, 2, 0);
    int v = pix(bm, 0, 0);
    if (v == BLACK_IDX) collapsed++;
    if (v >= 10 && v <= 245) in_range++;
    if (v >= 0 && v < 256) seen[v]++;
    SelectObject(dc, op);
    DeleteObject(pn);
  }
  CHECK_EQ(collapsed, 0);
  CHECK_EQ(in_range, 236);
  int distinct = 0;
  for (int i = 0; i < 256; i++) distinct += seen[i] != 0;
  CHECK_EQ(distinct, 236);
  /* Plain RGB never matches a reserved slot: black and near-black go to the static black. */
  CHECK_EQ(SetPixel(dc, 1, 1, GDI_RGB(0, 0, 0)), (uint32_t)GDI_RGB(0, 0, 0));
  CHECK_EQ(pix(bm, 1, 1), BLACK_IDX);
  CHECK_EQ(SetPixel(dc, 2, 1, GDI_RGB(1, 1, 1)), (uint32_t)GDI_RGB(0, 0, 0));
  CHECK_EQ(pix(bm, 2, 1), BLACK_IDX);
  /* A palette request for black also avoids the reserved slots. */
  CHECK_EQ(SetPixel(dc, 3, 1, GDI_PALETTERGB(0, 0, 0)), (uint32_t)GDI_RGB(0, 0, 0));
  CHECK_EQ(pix(bm, 3, 1), BLACK_IDX);
  SelectPalette(dc, oldpal, 0);
  CHECK_EQ(DeleteObject(pal), 1);
  GlobalUnlock(hg);
  GlobalFree(hg);
  free_dc(dc, bm);
}

/* Item 5a: with no clip set the region is the surface, so excluding half of it leaves ONE
 * rectangle (SIMPLEREGION) and excluding everything leaves NULLREGION. Windows' rule (a
 * rectangular region is SIMPLEREGION, an empty one NULLREGION); confident. An L-shape stays
 * COMPLEXREGION. */
static void test_exclude_clip_bounds(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  CHECK_EQ(ExcludeClipRect(dc, 0, 0, 4, 8), SIMPLEREGION);
  CHECK_EQ(ExcludeClipRect(dc, 4, 0, 8, 8), NULLREGION);
  CHECK_EQ(SelectClipRgn(dc, 0), SIMPLEREGION);
  CHECK_EQ(ExcludeClipRect(dc, 0, 0, 4, 4), COMPLEXREGION);
  CHECK_EQ(SelectClipRgn(dc, 0), SIMPLEREGION);
  /* A hole entirely outside the surface changes nothing. */
  CHECK_EQ(ExcludeClipRect(dc, 20, 20, 30, 30), SIMPLEREGION);
  free_dc(dc, bm);
}

static void test_objects_and_getobject(void) {
  HPEN16 pen = CreatePen(PS_DASH, 1, C_RED);
  CHECK(pen != 0);
  HBRUSH16 br = solid(GDI_RGB(1, 2, 3));
  CHECK(br != pen);
  HBITMAP16 bm;
  HDC16 dc = mk_dc(12, 5, &bm);
  uint16_t hg;
  FARPTR fp = far_alloc(32, &hg);
  uint8_t *p = LIN(fp);
  /* BITMAP (14 bytes): bmType, bmWidth, bmHeight, bmWidthBytes, bmPlanes, bmBitsPixel, bmBits. */
  CHECK_EQ(GetObject(bm, 14, fp), 14);
  CHECK_EQ(p[2] | (p[3] << 8), 12);
  CHECK_EQ(p[4] | (p[5] << 8), 5);
  CHECK_EQ(p[6] | (p[7] << 8), 12);        /* WORD-aligned 8-bpp row */
  CHECK_EQ(p[8], 1);
  CHECK_EQ(p[9], 8);
  /* LOGPEN (10 bytes): lopnStyle, lopnWidth.x, .y, lopnColor. */
  memset(p, 0xEE, 32);
  CHECK_EQ(GetObject(pen, 10, fp), 10);
  CHECK_EQ(p[0] | (p[1] << 8), PS_DASH);
  CHECK_EQ(p[2] | (p[3] << 8), 1);
  CHECK_EQ(p[6] | (p[7] << 8) | (p[8] << 16) | ((uint32_t)p[9] << 24), (int)C_RED);
  /* LOGBRUSH (8 bytes). */
  CHECK_EQ(GetObject(br, 8, fp), 8);
  CHECK_EQ(p[0] | (p[1] << 8), BS_SOLID);
  CHECK_EQ(p[2] | (p[3] << 8) | (p[4] << 16) | ((uint32_t)p[5] << 24), (int)GDI_RGB(1, 2, 3));
  CHECK_EQ(GetObject(0x7777, 8, fp), 0);   /* invalid handle */
  GlobalUnlock(hg);
  GlobalFree(hg);

  /* Selecting returns the previous object; deleting a selected object fails. */
  HGDIOBJ16 oldpen = SelectObject(dc, pen);
  CHECK_EQ(oldpen, GetStockObject(BLACK_PEN));
  CHECK_EQ(DeleteObject(pen), 0);
  CHECK_EQ(SelectObject(dc, oldpen), pen);
  CHECK_EQ(DeleteObject(pen), 1);
  CHECK_EQ(DeleteObject(pen), 0);          /* already gone */
  CHECK_EQ(DeleteObject(GetStockObject(BLACK_PEN)), 0);   /* stock objects stay */
  CHECK_EQ(GetStockObject(BLACK_PEN), GetStockObject(BLACK_PEN));
  CHECK(GetStockObject(WHITE_BRUSH) != GetStockObject(BLACK_BRUSH));
  CHECK_EQ(GetStockObject(99), 0);
  CHECK_EQ(DeleteObject(br), 1);
  /* A bitmap selected into a DC cannot be deleted either. */
  CHECK_EQ(DeleteObject(bm), 0);
  free_dc(dc, bm);
  /* Default attributes of a fresh memory DC. */
  HDC16 d2 = CreateCompatibleDC(g_scr);
  CHECK_EQ(GetBkColor(d2), (uint32_t)C_WHITE);
  CHECK_EQ(GetTextColor(d2), (uint32_t)C_BLACK);
  CHECK_EQ(GetBkMode(d2), OPAQUE);
  CHECK_EQ(SetBkColor(d2, C_BLUE), (uint32_t)C_WHITE);
  CHECK_EQ(GetBkColor(d2), (uint32_t)C_BLUE);
  CHECK_EQ(SetTextColor(d2, C_RED), (uint32_t)C_BLACK);
  CHECK_EQ(SetBkMode(d2, TRANSPARENT), OPAQUE);
  CHECK_EQ(DeleteDC(d2), 1);
  CHECK_EQ(DeleteDC(d2), 0);               /* stale handle */
}

static void test_caps_muldiv_escape(void) {
  CHECK_EQ(GetDeviceCaps(g_scr, HORZRES), 800);
  CHECK_EQ(GetDeviceCaps(g_scr, VERTRES), 600);
  CHECK_EQ(GetDeviceCaps(g_scr, BITSPIXEL), 8);
  CHECK_EQ(GetDeviceCaps(g_scr, PLANES), 1);
  CHECK_EQ(GetDeviceCaps(g_scr, SIZEPALETTE), 256);
  CHECK_EQ(GetDeviceCaps(g_scr, NUMRESERVED), 20);
  CHECK_EQ(GetDeviceCaps(g_scr, NUMCOLORS), 20);
  CHECK_EQ(GetDeviceCaps(g_scr, TECHNOLOGY), 1);        /* DT_RASDISPLAY */
  CHECK(GetDeviceCaps(g_scr, RASTERCAPS) & 0x100);      /* RC_PALETTE */
  CHECK_EQ(GetDeviceCaps(g_scr, LOGPIXELSX), 96);       /* assumption, see docs/inventory.md */
  CHECK_EQ(GetDeviceCaps(g_scr, LOGPIXELSY), 96);
  CHECK_EQ(GetDeviceCaps(g_scr, 9999), 0);
  /* A memory DC reports its selected bitmap's format. */
  HDC16 m = CreateCompatibleDC(g_scr);
  CHECK_EQ(GetDeviceCaps(m, BITSPIXEL), 1);
  CHECK_EQ(GetDeviceCaps(m, HORZRES), 800);
  CHECK_EQ(DeleteDC(m), 1);

  /* MulDiv(100, 3, 7) == 43 (rounded). The zero-divisor and overflow results follow Wine's
   * MulDiv16: wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
   * dlls/gdi.exe16/gdi.c lines 1920-1939 (return -32768 for a zero divisor at line 1923 and
   * for a result outside -32767..32767 at line 1937). */
  CHECK_EQ(MulDiv(100, 3, 7), 43);
  CHECK_EQ(MulDiv(100, 3, 0), -32768);
  CHECK_EQ(MulDiv(32767, 2, 1), -32768);
  CHECK_EQ(MulDiv(-32767, 1, 1), -32767);
  CHECK_EQ(MulDiv(-32768, 1, 1), -32768);   /* ret = -32768 < -32767 */
  CHECK_EQ(MulDiv(-100, 3, 7), -43);        /* round away from zero: (-300 - 3) / 7 */
  CHECK_EQ(MulDiv(100, 3, -7), -43);
  CHECK_EQ(MulDiv(-100, 3, -7), 43);
  CHECK_EQ(MulDiv(1, 1, 2), 1);             /* (1 + 1) / 2 */
  CHECK_EQ(MulDiv(-1, 1, 2), -1);

  /* Printing escapes return 0 until the printing task. */
  CHECK_EQ(Escape(g_scr, GDI_STARTDOC, 0, 0, 0), 0);
  CHECK_EQ(Escape(g_scr, GDI_ENDDOC, 0, 0, 0), 0);
  CHECK_EQ(Escape(g_scr, GDI_NEWFRAME, 0, 0, 0), 0);
}

static void test_present(void) {
  CHECK_EQ(fb_width(), 800);
  CHECK_EQ(fb_height(), 600);
  CHECK_EQ(fb_palette_entry(0), GDI_RGB(0, 0, 0));
  CHECK_EQ(fb_palette_entry(7), GDI_RGB(192, 192, 192));
  CHECK_EQ(fb_palette_entry(8), GDI_RGB(192, 220, 192));
  CHECK_EQ(fb_palette_entry(9), GDI_RGB(166, 202, 240));
  CHECK_EQ(fb_palette_entry(246), GDI_RGB(255, 251, 240));
  CHECK_EQ(fb_palette_entry(247), GDI_RGB(160, 160, 164));
  CHECK_EQ(fb_palette_entry(248), GDI_RGB(128, 128, 128));
  CHECK_EQ(fb_palette_entry(255), GDI_RGB(255, 255, 255));
  CHECK_EQ(fb_palette_entry(100), 0);       /* unused slot */
  CHECK_EQ(SetPixel(g_scr, 10, 20, C_RED), (uint32_t)C_RED);
  uint32_t *out = malloc(800u * 600u * 4u);
  CHECK(out != NULL);
  fb_present(out);
  CHECK_EQ(out[20 * 800 + 10], 0xFF0000FFu);          /* R,G,B,A = ff 00 00 ff */
  CHECK_EQ(SetPixel(g_scr, 11, 20, C_WHITE), (uint32_t)C_WHITE);
  fb_present(out);
  CHECK_EQ(out[20 * 800 + 11], 0xFFFFFFFFu);
  free(out);
}

/* ---------------------------------------------------------------- determinism */

static void scenario(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 16, &bm);
  HBRUSH16 b = solid(GDI_RGB(100, 40, 200));
  SelectObject(dc, b);
  Rectangle(dc, 1, 1, 12, 9);
  Ellipse(dc, 2, 2, 14, 14);
  ExcludeClipRect(dc, 4, 4, 8, 8);
  PatBlt(dc, 0, 0, 16, 16, PATINVERT);
  HDC16 s = gdi_create_screen_dc();
  BitBlt(s, 5, 5, 16, 16, dc, 0, 0, SRCCOPY);
  SelectObject(dc, GetStockObject(WHITE_BRUSH));
  DeleteDC(s);
  free_dc(dc, bm);
  DeleteObject(b);
}

static void test_replay_is_arena_exact(void) {
  /* All GDI state is in the arena: the same calls from a fresh arena leave identical bytes. */
  size_t n = mem_size();
  uint8_t *snap1 = malloc(n);
  gdi_shutdown();
  mem_shutdown();
  mem_init(ARENA_BYTES);
  CHECK_EQ(gdi_init(), 0);
  g_scr = gdi_create_screen_dc();
  scenario();
  memcpy(snap1, mem_arena(), n);
  gdi_shutdown();
  mem_shutdown();
  mem_init(ARENA_BYTES);
  CHECK_EQ(gdi_init(), 0);
  g_scr = gdi_create_screen_dc();
  scenario();
  CHECK_MEM(snap1, mem_arena(), n);
  free(snap1);
}

/* ---------------------------------------------------------------- harness thunks */

static uint32_t call_gdi(uint16_t ord, const uint16_t *args, int n) {
  uint32_t fp = harness_thunk_far("GDI", ord);
  CHECK(fp != 0);
  if (!fp) return 0;
  return harness_call_far(fp, HCONV_PASCAL, args, n);
}

static void test_harness_thunks(void) {
  /* MulDiv (GDI.128): PASCAL pushes a, b, c in order. */
  uint16_t a1[3] = {100, 3, 7};
  CHECK_EQ(call_gdi(128, a1, 3) & 0xFFFF, 43);
  uint16_t a2[3] = {100, 3, 0};
  CHECK_EQ(call_gdi(128, a2, 3) & 0xFFFF, 0x8000);
  /* GetDeviceCaps (GDI.80) on the screen DC. */
  uint16_t a3[2] = {g_scr, HORZRES};
  CHECK_EQ(call_gdi(80, a3, 2) & 0xFFFF, 800);
  /* CreateSolidBrush (GDI.66): COLORREF pushed high word first. */
  uint16_t a4[2] = {0x0000, 0x00FF};     /* high word 0, low word 0x00FF = red */
  uint32_t hb = call_gdi(66, a4, 2) & 0xFFFF;
  CHECK(hb != 0);
  /* COLORREF decoding: GetObject (GDI.82) on the brush reads the colour back as 0x000000FF.
   * A thunk that swaps the COLORREF words would store 0x00FF0000. */
  {
    uint16_t hgb;
    FARPTR lb = far_alloc(8, &hgb);
    uint16_t ga[4] = {(uint16_t)hb, 8, (uint16_t)(lb >> 16), (uint16_t)(lb & 0xFFFF)};
    CHECK_EQ(call_gdi(82, ga, 4) & 0xFFFF, 8);
    const uint8_t *q = LIN(lb);
    uint32_t col = (uint32_t)q[2] | ((uint32_t)q[3] << 8) | ((uint32_t)q[4] << 16) | ((uint32_t)q[5] << 24);
    CHECK_EQ(col, 0x000000FFu);
    GlobalUnlock(hgb);
    GlobalFree(hgb);
  }
  /* SetBkColor (GDI.1) returns the old colour in DX:AX: check both halves. */
  {
    HBITMAP16 bm0;
    HDC16 d0 = mk_dc(4, 4, &bm0);
    uint16_t s1[3] = {d0, 0x0012, 0x3456};          /* high word first */
    call_gdi(1, s1, 3);
    uint16_t s2[3] = {d0, 0x00AB, 0xCDEF};
    uint32_t oldc = call_gdi(1, s2, 3);
    CHECK_EQ(oldc & 0xFFFF, 0x3456u);                /* AX = low word */
    CHECK_EQ((oldc >> 16) & 0xFFFF, 0x0012u);        /* DX = high word */
    CHECK_EQ(oldc, 0x00123456u);
    free_dc(d0, bm0);
  }
  /* Signed coordinates: MoveTo (GDI.20) to x=-2 then LineTo (GDI.19) to x=4 on row 2 draws
   * pixels 0..3 (the off-surface part is clipped). A thunk that drops the sign turns -2 into
   * 65534 and draws from the far right instead. */
  {
    HBITMAP16 bm1;
    HDC16 d1 = mk_dc(8, 8, &bm1);
    uint16_t m1[3] = {d1, (uint16_t)-2, 2};
    call_gdi(20, m1, 3);
    uint16_t l1[3] = {d1, 4, 2};
    CHECK_EQ(call_gdi(19, l1, 3) & 0xFFFF, 1);
    CHECK_EQ(pix(bm1, 0, 2), BLACK_IDX);
    CHECK_EQ(pix(bm1, 3, 2), BLACK_IDX);
    CHECK_EQ(pix(bm1, 4, 2), WHITE_IDX);
    CHECK_EQ(pix(bm1, 5, 2), WHITE_IDX);
    /* The old position comes back with both coordinates sign-packed. */
    uint16_t m2[3] = {d1, (uint16_t)-5, (uint16_t)-3};
    call_gdi(20, m2, 3);
    uint16_t m3[3] = {d1, 0, 0};
    CHECK_EQ(call_gdi(20, m3, 3), 0xFFFDFFFBu);
    /* LineTo with a negative target: from (5,5) to (-1,5) covers x = 5..0 and stops at -1. */
    PatBlt(d1, 0, 0, 8, 8, WHITENESS);
    uint16_t m5[3] = {d1, 5, 5};
    call_gdi(20, m5, 3);
    uint16_t l5[3] = {d1, (uint16_t)-1, 5};
    call_gdi(19, l5, 3);
    CHECK_EQ(pix(bm1, 0, 5), BLACK_IDX);
    CHECK_EQ(pix(bm1, 5, 5), BLACK_IDX);
    CHECK_EQ(pix(bm1, 6, 5), WHITE_IDX);
    /* SetWindowOrg (GDI.11) with negative origin shifts drawing to the right/down. */
    PatBlt(d1, 0, 0, 8, 8, WHITENESS);
    uint16_t w1[3] = {d1, (uint16_t)-2, (uint16_t)-1};
    call_gdi(11, w1, 3);
    uint16_t m4[3] = {d1, 0, 0};
    call_gdi(20, m4, 3);
    uint16_t l4[3] = {d1, 3, 0};
    call_gdi(19, l4, 3);
    CHECK_EQ(pix(bm1, 1, 1), WHITE_IDX);
    CHECK_EQ(pix(bm1, 2, 1), BLACK_IDX);
    CHECK_EQ(pix(bm1, 4, 1), BLACK_IDX);
    CHECK_EQ(pix(bm1, 5, 1), WHITE_IDX);
    free_dc(d1, bm1);
  }
  /* MoveTo (GDI.20) returns the old position as DX:AX. */
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  uint16_t a5[3] = {dc, 3, 2};
  CHECK_EQ(call_gdi(20, a5, 3), 0);
  uint16_t a6[3] = {dc, 6, 2};
  CHECK_EQ(call_gdi(19, a6, 3) & 0xFFFF, 1);   /* LineTo (GDI.19) */
  CHECK_EQ(pix(bm, 3, 2), BLACK_IDX);
  CHECK_EQ(pix(bm, 5, 2), BLACK_IDX);
  CHECK_EQ(pix(bm, 6, 2), WHITE_IDX);
  uint16_t a7[3] = {dc, 0, 0};
  CHECK_EQ(call_gdi(20, a7, 3), (uint32_t)(6 | (2 << 16)));
  free_dc(dc, bm);
  uint16_t a8[1] = {(uint16_t)hb};
  CHECK_EQ(call_gdi(69, a8, 1) & 0xFFFF, 1);   /* DeleteObject */
}

int main(void) {
  /* Phase 1: the runtime alone, no harness. */
  mem_init(ARENA_BYTES);
  CHECK_EQ(gdi_init(), 0);
  g_scr = gdi_create_screen_dc();
  CHECK(g_scr != 0);

  test_rop3_all_256();
  test_rop2_all_16();
  test_lineto_end_exclusive();
  test_lineto_diagonal_vertical();
  test_rectangle_outline_only();
  test_rectangle_pen_and_brush();
  test_ellipse();
  test_patinvert_twice();
  test_rop2_xor_line_twice();
  test_dashed_pens_and_transparent();
  test_wide_pen_xor_no_double_hit();
  test_mono_to_colour_bitblt();
  test_bitblt_colour_overlap_and_rops();
  test_exclude_clip_rect_hole();
  test_intersect_and_region_clip();
  test_window_org_and_device_origin();
  test_stretchdibits_cargo_1to1();
  test_cargobmp_matches_resource();
  test_getdibits_roundtrip();
  test_dither_and_exact_brush();
  test_pattern_brush();
  test_palette();
  test_palette_reserved();
  test_exclude_clip_bounds();
  test_objects_and_getobject();
  test_caps_muldiv_escape();
  test_present();
  test_replay_is_arena_exact();

  gdi_shutdown();
  mem_shutdown();

  /* Phase 2: the PASCAL thunks of harness/gdi_thunks.c under the harness. */
  harness_set_quiet(true);
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "cannot load %s (run from the repo root)\n", EXE);
    return 1;
  }
  CHECK_EQ(gdi_init(), 0);
  gdi_register();
  g_scr = gdi_create_screen_dc();
  test_harness_thunks();
  gdi_shutdown();
  harness_shutdown();
  return TEST_DONE();
}
