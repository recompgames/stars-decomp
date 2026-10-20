/* Expectations for fonts and text output (runtime/fnt.h, font.c, text.c, fontdata.c and
 * harness/text_thunks.c), issue stars-0z4.5. Written before the implementation. Do not edit an
 * expectation to match the code: a disagreement is reported with both values.
 *
 * NO Windows font file exists here (owner amendment 2026-10-17). Two kinds of font are used:
 *   - a SYNTHETIC FNT built in this file ("TestFnt", 4 pixels high), so the decoder and the text
 *     functions have hand-checkable pixel expectations that depend on no real font;
 *   - the BUILT-IN look-alikes rasterised from OFL Liberation Sans (third_party/fonts/README.md).
 * Every pixel and metric expectation is internal: UNVERIFIED against Windows 3.1 (decision 9,
 * no reference captures yet). The Windows 3.1 mapping rule and DrawText layout rules are from
 * memory and the SDK text: UNVERIFIED too.
 *
 * Synthetic font TestFnt (version 2 or 3): chars 32..126, pixHeight 4, ascent 2 (so the
 * underline row is 3), default char '?'. Widths: 'A' 3, 'B' 2, 'H' 4, 'e' 3, 'l' 1, 'o' 3, ' ' 2,
 * 'F' 3, 'i' 1, '&' 2, every other character 3. Bitmaps (rows top to bottom):
 *   'A' .#. / #.# / ### / #.#     'B' ## / ## / ## / ##     'F' ### / #.. / ##. / #..
 *   'l' # / # / # / #             '?' ### / #.# / #.# / ###     all others blank.
 */
#include "test.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness/gdi_thunks.h"
#include "harness/harness.h"
#include "runtime/fnt.h"
#include "runtime/gdi.h"
#include "runtime/mem.h"
#include "runtime/text.h"

#define ARENA_BYTES (16u * 1024 * 1024)
#define EXE "original/stars26i.exe"
#define WHITE_IDX 255
#define BLACK_IDX 0
#define RED_IDX 249

static const COLORREF16 C_RED = GDI_RGB(255, 0, 0);

/* ---------------------------------------------------------------- synthetic FNT builder */

#define TF_FIRST 32
#define TF_LAST 126
#define TF_H 4

static int tf_width(int ch) {
  switch (ch) {
    case 'A': return 3;
    case 'B': return 2;
    case 'H': return 4;
    case 'e': return 3;
    case 'l': return 1;
    case 'o': return 3;
    case ' ': return 2;
    case 'F': return 3;
    case 'i': return 1;
    case '&': return 2;
    default: return 3;
  }
}

static const char *tf_rows(int ch) {
  switch (ch) {
    case 'A': return ".#." "#.#" "###" "#.#"; /* 4 rows x 3 */
    case 'B': return "########";         /* 4 rows x 2 */
    case 'F': return "###" "#.." "##." "#..";
    case 'l': return "####";
    case '?': return "###" "#.#" "#.#" "###";
    default: return NULL;
  }
}

static void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void p32(uint8_t *p, unsigned v) { p16(p, v & 0xffff); p16(p + 2, v >> 16); }

/* Builds the FNT image into buf (cap bytes); returns its size. version is 0x200 or 0x300. */
static size_t build_testfnt(uint8_t *buf, size_t cap, unsigned version) {
  const unsigned hdr = version == 0x200 ? 118 : 148, esz = version == 0x200 ? 4 : 6;
  const unsigned n = TF_LAST - TF_FIRST + 1;
  unsigned bits = hdr + (n + 1) * esz;
  unsigned off = bits;
  memset(buf, 0, cap);
  for (unsigned i = 0; i < n; i++) {
    int ch = (int)(TF_FIRST + i), w = tf_width(ch);
    p16(buf + hdr + i * esz, (unsigned)w);
    if (esz == 4) p16(buf + hdr + i * esz + 2, off); else p32(buf + hdr + i * esz + 2, off);
    const char *rows = tf_rows(ch);
    for (int st = 0; st < (w + 7) / 8; st++)
      for (int y = 0; y < TF_H; y++) {
        uint8_t v = 0;
        for (int x = 0; x < w; x++)
          if (rows && rows[y * w + x] == '#') v |= (uint8_t)(0x80 >> x);
        buf[off++] = v;
      }
  }
  /* sentinel entry */
  if (esz == 4) p16(buf + hdr + n * esz + 2, off); else p32(buf + hdr + n * esz + 2, off);
  unsigned face = off;
  memcpy(buf + off, "TestFnt", 8);
  off += 8;
  if (off > cap) abort();
  p16(buf, version);
  p32(buf + 2, off);
  p16(buf + 66, 0);
  p16(buf + 68, 3);   /* dfPoints */
  p16(buf + 70, 96);
  p16(buf + 72, 96);
  p16(buf + 74, 2);   /* dfAscent */
  p16(buf + 76, 1);   /* dfInternalLeading */
  p16(buf + 78, 1);   /* dfExternalLeading */
  p16(buf + 83, 400);
  p16(buf + 88, TF_H);
  buf[90] = 0x21;
  p16(buf + 91, 3);
  p16(buf + 93, 4);
  buf[95] = TF_FIRST;
  buf[96] = TF_LAST;
  buf[97] = (uint8_t)('?' - TF_FIRST);
  buf[98] = (uint8_t)(' ' - TF_FIRST);
  p32(buf + 105, face);
  p32(buf + 113, bits);
  return off;
}

static uint8_t g_tf2[2048], g_tf3[2048];
static size_t g_tf2_size, g_tf3_size;
static int g_tf_index = -1; /* registered v3 image */

/* ---------------------------------------------------------------- helpers */

static HDC16 g_scr;

static HDC16 mk_dc(int w, int h, HBITMAP16 *hbm_out) {
  HDC16 dc = CreateCompatibleDC(g_scr);
  HBITMAP16 bm = CreateCompatibleBitmap(g_scr, (int16_t)w, (int16_t)h);
  CHECK(dc != 0);
  CHECK(bm != 0);
  SelectObject(dc, bm);
  CHECK_EQ(PatBlt(dc, 0, 0, (int16_t)w, (int16_t)h, WHITENESS), 1);
  *hbm_out = bm;
  return dc;
}

static void free_dc(HDC16 dc, HBITMAP16 bm) {
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

static int legend(char c) {
  switch (c) {
    case '.': return WHITE_IDX;
    case '#': return BLACK_IDX;
    case 'r': return RED_IDX;
    default: return -1;
  }
}

/* Compares a rows x cols window at (x0, y0) with a grid of legend characters. */
static void expect_grid(HBITMAP16 bm, int x0, int y0, int rows, const char *const *grid, int line) {
  for (int y = 0; y < rows; y++)
    for (int x = 0; grid[y][x]; x++)
      if (pix(bm, x0 + x, y0 + y) != legend(grid[y][x])) {
        fprintf(stderr, "test_font.c:%d: grid mismatch at (%d,%d): got %d want %d\n", line, x0 + x, y0 + y,
                pix(bm, x0 + x, y0 + y), legend(grid[y][x]));
        test_checks_run++;
        test_checks_failed++;
        return;
      }
  test_checks_run++;
}
#define GRID(bm, x0, y0, ...)                                       \
  do {                                                              \
    static const char *const g_[] = {__VA_ARGS__};                  \
    expect_grid((bm), (x0), (y0), (int)(sizeof g_ / sizeof g_[0]), g_, __LINE__); \
  } while (0)

static FARPTR far_alloc(uint32_t n) {
  uint16_t h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, n);
  CHECK(h != 0);
  return GlobalLock(h);
}

static FARPTR far_str(const char *s) {
  FARPTR fp = far_alloc((uint32_t)strlen(s) + 1);
  memcpy(LIN(fp), s, strlen(s) + 1);
  return fp;
}

static FARPTR far_rect(int l, int t, int r, int b) {
  FARPTR fp = far_alloc(8);
  RECT16 rc = {(int16_t)l, (int16_t)t, (int16_t)r, (int16_t)b};
  memcpy(LIN(fp), &rc, 8);
  return fp;
}

static RECT16 get_rect(FARPTR fp) {
  RECT16 r;
  memcpy(&r, LIN(fp), 8);
  return r;
}

/* Builds a 50-byte LOGFONT. */
static void mk_logfont(uint8_t *lf, int height, int weight, const char *face) {
  memset(lf, 0, LOGFONT16_SIZE);
  p16(lf + LF_HEIGHT, (unsigned)(uint16_t)height);
  p16(lf + LF_WEIGHT, (unsigned)weight);
  strncpy((char *)lf + LF_FACENAME, face, LF_FACENAME_LEN - 1);
}

static HFONT16 mk_font(int height, int weight, const char *face) {
  uint8_t lf[LOGFONT16_SIZE];
  mk_logfont(lf, height, weight, face);
  FARPTR fp = far_alloc(LOGFONT16_SIZE);
  memcpy(LIN(fp), lf, sizeof lf);
  HFONT16 h = CreateFontIndirect(fp);
  CHECK(h != 0);
  return h;
}

static int metric(HDC16 dc, int off) {
  FARPTR tm = far_alloc(TEXTMETRIC16_SIZE);
  CHECK_EQ(GetTextMetrics(dc, tm), 1);
  const uint8_t *p = LIN(tm);
  return off < 16 ? (int16_t)(p[off] | (p[off + 1] << 8)) : p[off];
}
#define TM_HEIGHT 0
#define TM_ASCENT 2
#define TM_DESCENT 4
#define TM_INTLEAD 6
#define TM_EXTLEAD 8
#define TM_AVGW 10
#define TM_MAXW 12
#define TM_WEIGHT 14
#define TM_ITALIC 16

static void setup_testfont(HDC16 dc) {
  HFONT16 f = mk_font(-3, 400, "TestFnt");
  SelectObject(dc, f);
}

/* ---------------------------------------------------------------- FNT decoder */

static void test_fnt_decoder(void) {
  unsigned versions[2] = {0x200, 0x300};
  uint8_t *imgs[2] = {g_tf2, g_tf3};
  size_t sizes[2] = {g_tf2_size, g_tf3_size};
  for (int v = 0; v < 2; v++) {
    FntFont f;
    CHECK_EQ(fnt_parse(imgs[v], sizes[v], &f), 0);
    CHECK_EQ(f.version, versions[v]);
    CHECK_EQ(f.pix_height, 4);
    CHECK_EQ(f.ascent, 2);
    CHECK_EQ(f.internal_leading, 1);
    CHECK_EQ(f.external_leading, 1);
    CHECK_EQ(f.points, 3);
    CHECK_EQ(f.weight, 400);
    CHECK_EQ(f.first, 32);
    CHECK_EQ(f.last, 126);
    CHECK_EQ(f.default_char, '?');
    CHECK_EQ(f.break_char, ' ');
    CHECK(strcmp(f.face, "TestFnt") == 0);
    CHECK_EQ(fnt_glyph_width(&f, 'A'), 3);
    CHECK_EQ(fnt_glyph_width(&f, 'B'), 2);
    CHECK_EQ(fnt_glyph_width(&f, 'l'), 1);
    CHECK_EQ(fnt_glyph_width(&f, 'H'), 4);
    /* 'A' = .#. / #.# / ### / #.# */
    CHECK(!fnt_glyph_pixel(&f, 'A', 0, 0));
    CHECK(fnt_glyph_pixel(&f, 'A', 1, 0));
    CHECK(fnt_glyph_pixel(&f, 'A', 0, 1));
    CHECK(!fnt_glyph_pixel(&f, 'A', 1, 1));
    CHECK(fnt_glyph_pixel(&f, 'A', 2, 2));
    CHECK(fnt_glyph_pixel(&f, 'A', 2, 3));
    CHECK(!fnt_glyph_pixel(&f, 'A', 3, 2));  /* outside the glyph box */
    CHECK(!fnt_glyph_pixel(&f, 'A', 0, 4));
    /* A character outside [first, last] uses the default character '?' (3 wide, a box). */
    CHECK_EQ(fnt_glyph_width(&f, 200), 3);
    CHECK(fnt_glyph_pixel(&f, 200, 1, 0));
    CHECK(!fnt_glyph_pixel(&f, 200, 1, 1));
  }
  /* A 9-pixel-wide glyph uses two column strips: build a minimal FNT v2 by hand. */
  {
    uint8_t b[300];
    memset(b, 0, sizeof b);
    unsigned bits = 118 + 2 * 4;
    p16(b, 0x200);
    p16(b + 88, 2);
    b[95] = 'x';
    b[96] = 'x';
    p16(b + 118, 9);
    p16(b + 120, bits);
    p16(b + 122 + 2, bits + 4);
    b[bits + 0] = 0x80;  /* strip 0 row 0: pixel x=0 */
    b[bits + 1] = 0x01;  /* strip 0 row 1: pixel x=7 */
    b[bits + 2] = 0x80;  /* strip 1 row 0: pixel x=8 */
    b[bits + 3] = 0x00;
    FntFont f;
    CHECK_EQ(fnt_parse(b, bits + 4, &f), 0);
    CHECK_EQ(fnt_glyph_width(&f, 'x'), 9);
    CHECK(fnt_glyph_pixel(&f, 'x', 0, 0));
    CHECK(fnt_glyph_pixel(&f, 'x', 7, 1));
    CHECK(fnt_glyph_pixel(&f, 'x', 8, 0));
    CHECK(!fnt_glyph_pixel(&f, 'x', 8, 1));
    CHECK(!fnt_glyph_pixel(&f, 'x', 1, 0));
  }
  /* Malformed resources are rejected. */
  {
    FntFont f;
    uint8_t bad[2048];
    memcpy(bad, g_tf3, g_tf3_size);
    CHECK(fnt_parse(bad, 10, &f) < 0);                     /* truncated header */
    CHECK(fnt_parse(bad, g_tf3_size - 40, &f) < 0);        /* bitmaps run past the end */
    p16(bad, 0x0100);
    CHECK(fnt_parse(bad, g_tf3_size, &f) < 0);             /* unknown version */
    memcpy(bad, g_tf3, g_tf3_size);
    p16(bad + 66, 1);
    CHECK(fnt_parse(bad, g_tf3_size, &f) < 0);             /* vector font */
    memcpy(bad, g_tf3, g_tf3_size);
    bad[96] = 20;                                           /* last < first */
    CHECK(fnt_parse(bad, g_tf3_size, &f) < 0);
    CHECK(fnt_parse(NULL, 100, &f) < 0);
  }
}

/* ---------------------------------------------------------------- built-in look-alike fonts */

static void test_builtin_fonts(void) {
  CHECK(text_font_count() >= 3);
  /* Font 0 is System, font 1 is MS Sans Serif 8 pt. UNVERIFIED: dfPixHeight 16 and 13 follow the
   * Windows 3.1 System and MS Sans Serif 8 point sizes at 96 dpi from memory. */
  const FntFont *sys = text_font_info(0), *sans = text_font_info(1);
  CHECK(sys && sans);
  if (!sys || !sans) return;
  CHECK(strcmp(sys->face, "System") == 0);
  CHECK(strcmp(sans->face, "MS Sans Serif") == 0);
  CHECK_EQ(sans->points, 8);
  CHECK_EQ(sans->pix_height, 13);
  CHECK_EQ(sys->pix_height, 16);
  CHECK(fnt_glyph_width(sans, 'A') > 0);
  CHECK(fnt_glyph_width(sys, 'A') > 0);
  CHECK_EQ(sans->first, 32);
  CHECK_EQ(sans->last, 255);
  CHECK(sans->weight < 600);
  CHECK(sys->weight >= 600);
  /* 'A' has ink; ' ' has none. */
  int ink = 0, ink_space = 0;
  for (int y = 0; y < sans->pix_height; y++)
    for (int x = 0; x < fnt_glyph_width(sans, 'A'); x++) ink += fnt_glyph_pixel(sans, 'A', x, y);
  for (int y = 0; y < sans->pix_height; y++)
    for (int x = 0; x < fnt_glyph_width(sans, ' '); x++) ink_space += fnt_glyph_pixel(sans, ' ', x, y);
  CHECK(ink > 8);
  CHECK_EQ(ink_space, 0);
  /* Arial family exists in regular and bold. */
  int arial_reg = 0, arial_bold = 0;
  for (int i = 0; i < text_font_count(); i++) {
    const FntFont *f = text_font_info(i);
    if (strcmp(f->face, "Arial") == 0) { if (f->weight >= 600) arial_bold++; else arial_reg++; }
  }
  CHECK(arial_reg >= 3);
  CHECK(arial_bold >= 3);
}

/* ---------------------------------------------------------------- GDI text */

static void test_textout_synthetic(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  FARPTR s = far_str("AB");
  CHECK_EQ(TextOut(dc, 1, 1, s, 2), 1);
  /* A at x 1..3, B at x 4..5, rows 1..4. */
  GRID(bm, 0, 0, "........",
                 "..#.##..",
                 ".#.###..",
                 ".#####..",
                 ".#.###..",
                 "........");
  free_dc(dc, bm);

  /* Opaque background: the whole cell (advance x height) takes the background colour. */
  dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  SetBkColor(dc, C_RED);
  SetBkMode(dc, OPAQUE);
  CHECK_EQ(TextOut(dc, 1, 1, s, 2), 1);
  GRID(bm, 0, 0, "........",
                 ".r#r##..",
                 ".#r###..",
                 ".#####..",
                 ".#r###..",
                 "........");
  free_dc(dc, bm);
}

static void test_textout_transparent_keeps_background(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  HBRUSH16 red = CreateSolidBrush(C_RED);
  HGDIOBJ16 oldb = SelectObject(dc, red);
  PatBlt(dc, 0, 0, 16, 8, PATCOPY);
  SetBkMode(dc, TRANSPARENT);
  FARPTR s = far_str("AB");
  CHECK_EQ(TextOut(dc, 1, 1, s, 2), 1);
  GRID(bm, 0, 0, "rrrrrrrr",
                 "rr#r##rr",
                 "r#r###rr",
                 "r#####rr",
                 "r#r###rr",
                 "rrrrrrrr");
  SelectObject(dc, oldb);
  free_dc(dc, bm);
  DeleteObject(red);
}

static void test_extent_and_metrics(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  setup_testfont(dc);
  /* "Hello" = H4 + e3 + l1 + l1 + o3 = 12. */
  uint32_t ext = GetTextExtent(dc, far_str("Hello"), 5);
  CHECK_EQ(ext & 0xFFFF, 12);
  CHECK_EQ(ext >> 16, 4);
  CHECK_EQ(GetTextExtent(dc, far_str("Hello"), 3) & 0xFFFF, 8); /* count limits the run */
  CHECK_EQ(GetTextExtent(dc, far_str(""), 0), 4u << 16);
  CHECK_EQ(metric(dc, TM_HEIGHT), 4);
  CHECK_EQ(metric(dc, TM_ASCENT), 2);
  CHECK_EQ(metric(dc, TM_DESCENT), 2);
  CHECK_EQ(metric(dc, TM_INTLEAD), 1);
  CHECK_EQ(metric(dc, TM_EXTLEAD), 1);
  CHECK_EQ(metric(dc, TM_AVGW), 3);
  CHECK_EQ(metric(dc, TM_MAXW), 4);
  CHECK_EQ(metric(dc, TM_WEIGHT), 400);
  CHECK_EQ(metric(dc, 19), 32);   /* tmFirstChar */
  CHECK_EQ(metric(dc, 20), 126);  /* tmLastChar */
  CHECK_EQ(metric(dc, 21), '?');  /* tmDefaultChar (absolute) */
  CHECK_EQ(metric(dc, 22), ' ');  /* tmBreakChar */
  free_dc(dc, bm);

  /* Built-in MS Sans Serif 8 pt: UNVERIFIED metrics of the look-alike. */
  dc = mk_dc(8, 8, &bm);
  SelectObject(dc, GetStockObject(ANSI_VAR_FONT));
  CHECK_EQ(metric(dc, TM_HEIGHT), 13);
  CHECK_EQ(metric(dc, TM_ASCENT), 11);
  CHECK_EQ(metric(dc, TM_DESCENT), 2);
  CHECK_EQ(metric(dc, TM_INTLEAD), 2);
  CHECK_EQ(metric(dc, TM_WEIGHT), 400);
  CHECK_EQ(metric(dc, 19), 32);
  CHECK_EQ(metric(dc, 20), 255);
  const FntFont *sans = text_font_info(1);
  const char *hello = "Hello";
  int sum = 0;
  for (const char *p = hello; *p; p++) sum += fnt_glyph_width(sans, (uint8_t)*p);
  CHECK_EQ(GetTextExtent(dc, far_str(hello), 5) & 0xFFFF, sum);
  CHECK_EQ(GetTextExtent(dc, far_str(hello), 5) >> 16, 13);
  free_dc(dc, bm);
}

static void test_textout_builtin_mask(void) {
  /* TextOut "Ag" at (0,0), black on white on a 32x16 surface: the pixel mask equals the FNT glyph
   * bitmaps of the font the mapper picks for MS Sans Serif. */
  HBITMAP16 bm;
  HDC16 dc = mk_dc(32, 16, &bm);
  SelectObject(dc, mk_font(-11, 400, "MS Sans Serif"));
  CHECK_EQ(TextOut(dc, 0, 0, far_str("Ag"), 2), 1);
  const FntFont *f = text_font_info(1);
  int pen = 0, mismatches = 0, ink = 0;
  for (const char *p = "Ag"; *p; p++) {
    int w = fnt_glyph_width(f, (uint8_t)*p);
    for (int y = 0; y < f->pix_height; y++)
      for (int x = 0; x < w; x++) {
        bool on = fnt_glyph_pixel(f, (uint8_t)*p, x, y);
        ink += on;
        if (pix(bm, pen + x, y) != (on ? BLACK_IDX : WHITE_IDX)) mismatches++;
      }
    pen += w;
  }
  CHECK_EQ(mismatches, 0);
  CHECK(ink > 15);
  /* Nothing outside the two cells was touched. */
  int stray = 0;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 32; x++)
      if ((x >= pen || y >= f->pix_height) && pix(bm, x, y) != WHITE_IDX) stray++;
  CHECK_EQ(stray, 0);
  free_dc(dc, bm);
}

static void test_exttextout(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  SetBkColor(dc, C_RED);
  SetBkMode(dc, TRANSPARENT);
  /* ETO_OPAQUE fills the rectangle with the background colour, text then draws transparent. */
  CHECK_EQ(ExtTextOut(dc, 1, 1, ETO_OPAQUE, far_rect(0, 0, 8, 6), far_str("AB"), 2, 0), 1);
  GRID(bm, 0, 0, "rrrrrrrr.",
                 "rr#r##rr.",
                 "r#r###rr.",
                 "r#####rr.",
                 "r#r###rr.",
                 "rrrrrrrr.");
  free_dc(dc, bm);

  /* ETO_CLIPPED: glyph pixels outside the rectangle are not drawn. Rect x 0..2 (right exclusive). */
  dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  CHECK_EQ(ExtTextOut(dc, 1, 1, ETO_CLIPPED, far_rect(0, 0, 3, 8), far_str("AB"), 2, 0), 1);
  GRID(bm, 0, 0, "........",
                 "..#.....",
                 ".#......",
                 ".##.....",
                 ".#......");
  /* Without ETO_CLIPPED the rectangle is ignored. */
  PatBlt(dc, 0, 0, 16, 8, WHITENESS);
  CHECK_EQ(ExtTextOut(dc, 1, 1, 0, far_rect(0, 0, 3, 8), far_str("AB"), 2, 0), 1);
  CHECK_EQ(pix(bm, 4, 1), BLACK_IDX);
  free_dc(dc, bm);

  /* The dx array overrides the advances: "AB" with advances 5 and 2 puts B at x = 1 + 5. */
  dc = mk_dc(16, 8, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  FARPTR dx = far_alloc(4);
  ((int16_t *)LIN(dx))[0] = 5;
  ((int16_t *)LIN(dx))[1] = 2;
  CHECK_EQ(ExtTextOut(dc, 1, 1, 0, 0, far_str("AB"), 2, dx), 1);
  GRID(bm, 0, 0, "........",
                 "..#...##",
                 ".#.#..##",
                 ".###..##",
                 ".#.#..##");
  free_dc(dc, bm);
}

/* ---------------------------------------------------------------- DrawText */

static void test_drawtext(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(100, 20, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  FARPTR rc = far_rect(0, 0, 100, 20);
  /* DT_CENTER | DT_SINGLELINE | DT_VCENTER: x = (100 - 5) / 2 = 47, y = (20 - 4) / 2 = 8. */
  CHECK_EQ(DrawText(dc, far_str("AB"), -1, rc, DT_CENTER | DT_SINGLELINE | DT_VCENTER), 4);
  CHECK_EQ(pix(bm, 48, 8), BLACK_IDX);   /* A top middle */
  CHECK_EQ(pix(bm, 47, 8), WHITE_IDX);   /* A top left is blank */
  CHECK_EQ(pix(bm, 47, 9), BLACK_IDX);
  CHECK_EQ(pix(bm, 49, 8), WHITE_IDX);   /* A top right is blank; B starts at 47 + 3 = 50 */
  CHECK_EQ(pix(bm, 50, 8), BLACK_IDX);
  CHECK_EQ(pix(bm, 51, 11), BLACK_IDX);
  CHECK_EQ(pix(bm, 52, 11), WHITE_IDX);
  CHECK_EQ(pix(bm, 46, 9), WHITE_IDX);
  CHECK_EQ(pix(bm, 48, 7), WHITE_IDX);
  CHECK_EQ(pix(bm, 48, 12), WHITE_IDX);
  /* Rounding: width 5, rect 100 wide: (100 - 5) / 2 = 47 (integer division). */
  PatBlt(dc, 0, 0, 100, 20, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("B"), -1, rc, DT_CENTER | DT_SINGLELINE), 4);
  CHECK_EQ(pix(bm, 49, 0), BLACK_IDX);   /* B is 2 wide: (100 - 2) / 2 = 49 */
  CHECK_EQ(pix(bm, 48, 0), WHITE_IDX);
  /* DT_RIGHT and DT_BOTTOM. */
  PatBlt(dc, 0, 0, 100, 20, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("AB"), -1, rc, DT_RIGHT | DT_BOTTOM | DT_SINGLELINE), 4);
  CHECK_EQ(pix(bm, 96, 16), BLACK_IDX);  /* A starts at 100 - 5 = 95, top middle at 96 */
  CHECK_EQ(pix(bm, 96, 15), WHITE_IDX);
  CHECK_EQ(pix(bm, 99, 19), BLACK_IDX);  /* B bottom right */
  /* Default (left, top): the text starts at the rectangle's top left. */
  PatBlt(dc, 0, 0, 100, 20, WHITENESS);
  FARPTR rc2 = far_rect(10, 5, 60, 15);
  DrawText(dc, far_str("A"), -1, rc2, 0);
  CHECK_EQ(pix(bm, 11, 5), BLACK_IDX);
  CHECK_EQ(pix(bm, 10, 5), WHITE_IDX);
  /* DT_CALCRECT does not draw; single line: right = left + width, bottom = top + height. */
  PatBlt(dc, 0, 0, 100, 20, WHITENESS);
  FARPTR rc3 = far_rect(10, 5, 60, 6);
  CHECK_EQ(DrawText(dc, far_str("AB"), -1, rc3, DT_CALCRECT | DT_SINGLELINE), 4);
  RECT16 r = get_rect(rc3);
  CHECK_EQ(r.left, 10);
  CHECK_EQ(r.top, 5);
  CHECK_EQ(r.right, 15);
  CHECK_EQ(r.bottom, 9);
  CHECK_EQ(pix(bm, 11, 5), WHITE_IDX);
  /* Clipping: text is clipped to the rectangle unless DT_NOCLIP. */
  FARPTR rc4 = far_rect(10, 5, 12, 15);
  DrawText(dc, far_str("AB"), -1, rc4, 0);
  CHECK_EQ(pix(bm, 11, 5), BLACK_IDX);
  CHECK_EQ(pix(bm, 12, 6), WHITE_IDX);
  DrawText(dc, far_str("AB"), -1, rc4, DT_NOCLIP);
  CHECK_EQ(pix(bm, 12, 6), BLACK_IDX);
  free_dc(dc, bm);
}

static void test_drawtext_prefix(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(40, 10, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  FARPTR rc = far_rect(0, 0, 40, 10);
  /* "&File": F gets the underline (row ascent + 1 = 3). F is "###/#../##./#.." so row 3 gains
   * columns 1 and 2. The '&' itself is not drawn and not measured: F starts at x = 0. */
  CHECK_EQ(DrawText(dc, far_str("&File"), -1, rc, DT_SINGLELINE), 4);
  GRID(bm, 0, 0, "###",
                 "#..",
                 "##.",
                 "###");
  CHECK_EQ(pix(bm, 3, 3), WHITE_IDX); /* 'i' is not underlined */
  CHECK_EQ(pix(bm, 4, 3), BLACK_IDX); /* 'l' ink, not underline */
  /* Same string with DT_NOPREFIX prints the '&' literally (2 blank columns), F starts at x = 2
   * and nothing is underlined. */
  PatBlt(dc, 0, 0, 40, 10, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("&File"), -1, rc, DT_SINGLELINE | DT_NOPREFIX), 4);
  GRID(bm, 0, 0, "..###",
                 "..#..",
                 "..##.",
                 "..#..");
  /* "&&F": a literal ampersand (2 columns, blank) then F at x = 2, no underline. */
  PatBlt(dc, 0, 0, 40, 10, WHITENESS);
  DrawText(dc, far_str("&&F"), -1, rc, DT_SINGLELINE);
  GRID(bm, 0, 0, "..###",
                 "..#..",
                 "..##.",
                 "..#..");
  /* The width used for DT_CENTER excludes the '&': "&F" is 3 wide, x = (40 - 3) / 2 = 18. */
  PatBlt(dc, 0, 0, 40, 10, WHITENESS);
  DrawText(dc, far_str("&F"), -1, rc, DT_SINGLELINE | DT_CENTER);
  CHECK_EQ(pix(bm, 18, 0), BLACK_IDX);
  CHECK_EQ(pix(bm, 17, 0), WHITE_IDX);
  free_dc(dc, bm);
}

static void test_drawtext_lines(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(40, 30, &bm);
  setup_testfont(dc);
  SetBkMode(dc, TRANSPARENT);
  /* Newline: two lines of height 4. */
  FARPTR rc = far_rect(0, 0, 40, 30);
  CHECK_EQ(DrawText(dc, far_str("A\nB"), -1, rc, 0), 8);
  CHECK_EQ(pix(bm, 1, 0), BLACK_IDX);   /* A */
  CHECK_EQ(pix(bm, 0, 4), BLACK_IDX);   /* B on line 2, x from 0 */
  CHECK_EQ(pix(bm, 1, 7), BLACK_IDX);
  /* DT_SINGLELINE ignores the newline. */
  PatBlt(dc, 0, 0, 40, 30, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("A\nB"), -1, rc, DT_SINGLELINE), 4);
  CHECK_EQ(pix(bm, 3, 0), BLACK_IDX);   /* B right after A */
  CHECK_EQ(pix(bm, 0, 4), WHITE_IDX);
  /* DT_EXTERNALLEADING adds the font's external leading (1) per line. */
  PatBlt(dc, 0, 0, 40, 30, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("A\nB"), -1, rc, DT_EXTERNALLEADING), 10);
  CHECK_EQ(pix(bm, 0, 5), BLACK_IDX);
  /* DT_WORDBREAK: "AB AB" (3+2+2+3+2 = 12) in 8 pixels breaks after the first "AB". */
  PatBlt(dc, 0, 0, 40, 30, WHITENESS);
  FARPTR rc2 = far_rect(0, 0, 8, 30);
  CHECK_EQ(DrawText(dc, far_str("AB AB"), -1, rc2, DT_WORDBREAK), 8);
  CHECK_EQ(pix(bm, 1, 0), BLACK_IDX);   /* A, line 1 */
  CHECK_EQ(pix(bm, 1, 4), BLACK_IDX);   /* A, line 2 starts at x = 0 */
  CHECK_EQ(pix(bm, 7, 1), WHITE_IDX);   /* the second AB did not stay on line 1 */
  /* DT_WORDBREAK | DT_CALCRECT keeps the width and extends the bottom. */
  FARPTR rc3 = far_rect(0, 0, 8, 1);
  CHECK_EQ(DrawText(dc, far_str("AB AB"), -1, rc3, DT_WORDBREAK | DT_CALCRECT), 8);
  RECT16 r = get_rect(rc3);
  CHECK_EQ(r.right, 8);
  CHECK_EQ(r.bottom, 8);
  /* A word wider than the line is broken between characters. */
  FARPTR rc4 = far_rect(0, 0, 4, 1);
  CHECK_EQ(DrawText(dc, far_str("ABB"), -1, rc4, DT_WORDBREAK | DT_CALCRECT), 8);
  /* Tabs: with DT_EXPANDTABS a tab advances to the next multiple of 8 * average width (24). */
  PatBlt(dc, 0, 0, 40, 30, WHITENESS);
  CHECK_EQ(DrawText(dc, far_str("A\tB"), -1, rc, DT_EXPANDTABS | DT_SINGLELINE), 4);
  CHECK_EQ(pix(bm, 24, 0), BLACK_IDX);
  CHECK_EQ(pix(bm, 25, 3), BLACK_IDX);
  free_dc(dc, bm);
}

/* ---------------------------------------------------------------- fonts: mapping, objects */

/* The LOGFONTs the game builds (docs/inventory.md "LOGFONTs"), heights at LOGPIXELSY = 96:
 * -MulDiv(pt, 96, 72). Faces come from the game's string table entries 0x51b-0x51e, recovered by
 * the reviewer from the game's decoder: "Arial", "Arial Bold", "Arial Italic", "Arial Bold Italic".
 * Rotation (lfEscapement 3150) is ignored by raster fonts, so the rotated font maps like the
 * unrotated one. The title font (TITLEWNDPROC 4:0x60f6) takes lfHeight from a helper divided by
 * -3; that helper value was NOT recovered, so -16 here is a documented TEST INPUT, not a game
 * value. */
static const struct {
  const char *what;
  const char *face;
  int height, weight, escapement, want_cell, want_bold, want_italic;
} k_game_fonts[] = {
    {"6 pt (FUN_1000_08a4)", "Arial", -8, 0, 0, 9, 0, 0},
    {"7 pt (FUN_1000_08a4)", "Arial", -9, 0, 0, 10, 0, 0},
    {"10 pt regular (loop 1 entry 0)", "Arial", -13, 0, 0, 15, 0, 0},
    {"10 pt bold (loop 1 entry 1)", "Arial Bold", -13, 0, 0, 15, 1, 0},
    {"8 pt Arial (loop 2 entry 0)", "Arial", -11, 0, 0, 12, 0, 0},
    {"8 pt Arial Bold (loop 2 entry 1)", "Arial Bold", -11, 0, 0, 12, 1, 0},
    {"8 pt Arial Italic (loop 2 entry 2)", "Arial Italic", -11, 0, 0, 12, 0, 1},
    {"8 pt Arial Bold Italic (loop 2 entry 3)", "Arial Bold Italic", -11, 0, 0, 12, 1, 1},
    {"8 pt rotated Arial Bold (DAT_2936, escapement 3150; rotation ignored)", "Arial Bold", -11, 0, 3150, 12, 1, 0},
    {"24 px Arial Bold (FUN_10d8_36cc / 37ac)", "Arial Bold", -24, 0, 0, 27, 1, 0},
    {"fit-to-box bold (FUN_1038_9482), weight 900", "Arial Bold", -13, 900, 0, 15, 1, 0},
    {"title font Arial Bold Italic (4:0x60f6; height is a test input)", "Arial Bold Italic", -16, 0, 0, 17, 1, 1},
};

static void test_font_mapping(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  for (unsigned i = 0; i < sizeof k_game_fonts / sizeof k_game_fonts[0]; i++) {
    uint8_t glf[LOGFONT16_SIZE];
    mk_logfont(glf, k_game_fonts[i].height, k_game_fonts[i].weight, k_game_fonts[i].face);
    p16(glf + LF_ESCAPEMENT, (unsigned)k_game_fonts[i].escapement);
    FARPTR gfp = far_alloc(LOGFONT16_SIZE);
    memcpy(LIN(gfp), glf, sizeof glf);
    HFONT16 f = CreateFontIndirect(gfp);
    CHECK(f != 0);
    SelectObject(dc, f);
    int h = metric(dc, TM_HEIGHT);
    CHECK_EQ(metric(dc, TM_WEIGHT) >= 600, k_game_fonts[i].want_bold);
    CHECK_EQ(metric(dc, TM_ITALIC), k_game_fonts[i].want_italic);
    CHECK(strcmp(text_font_info(text_map_logfont(glf))->face, "Arial") == 0);
    if (h != k_game_fonts[i].want_cell) fprintf(stderr, "mapping: %s -> tmHeight %d\n", k_game_fonts[i].what, h);
    CHECK_EQ(h, k_game_fonts[i].want_cell);
    /* tmHeight equals the mapped font's dfPixHeight. */
    uint8_t lf[LOGFONT16_SIZE];
    mk_logfont(lf, k_game_fonts[i].height, k_game_fonts[i].weight, k_game_fonts[i].face);
    CHECK_EQ(text_font_info(text_map_logfont(lf))->pix_height, h);
    SelectObject(dc, GetStockObject(SYSTEM_FONT));
    CHECK_EQ(DeleteObject(f), 1);
  }
  /* "Arial Bold" resolves to the bold Arial fonts, "Arial" with weight 0 to the regular ones. */
  uint8_t lf[LOGFONT16_SIZE];
  mk_logfont(lf, -13, 0, "Arial Bold");
  CHECK(text_font_info(text_map_logfont(lf))->weight >= 600);
  mk_logfont(lf, -13, 0, "arial");
  CHECK(text_font_info(text_map_logfont(lf))->weight < 600);
  mk_logfont(lf, -13, 700, "Arial");
  CHECK(text_font_info(text_map_logfont(lf))->weight >= 600);
  /* Nearest height not above the request: -12 takes the 11 pixel font (cell 12), not 13. */
  mk_logfont(lf, -12, 0, "Arial");
  CHECK_EQ(text_font_info(text_map_logfont(lf))->pix_height, 12);
  /* Every candidate taller than the request: the smallest is used (em 8, cell 9). */
  mk_logfont(lf, -5, 0, "Arial");
  CHECK_EQ(text_font_info(text_map_logfont(lf))->pix_height, 9);
  /* A positive height is a cell height: 14 takes the largest cell <= 14, that is 12 (em 11). */
  mk_logfont(lf, 14, 0, "Arial");
  CHECK_EQ(text_font_info(text_map_logfont(lf))->pix_height, 12);
  /* MS Sans Serif has one size: any request maps to it; asking for bold still finds it. */
  mk_logfont(lf, -40, 700, "MS Sans Serif");
  CHECK(strcmp(text_font_info(text_map_logfont(lf))->face, "MS Sans Serif") == 0);
  mk_logfont(lf, -8, 0, "Helv");
  CHECK(strcmp(text_font_info(text_map_logfont(lf))->face, "MS Sans Serif") == 0);
  /* Unknown or empty face falls back to System. */
  mk_logfont(lf, -13, 0, "No Such Face");
  CHECK(strcmp(text_font_info(text_map_logfont(lf))->face, "System") == 0);
  mk_logfont(lf, -13, 0, "");
  CHECK(strcmp(text_font_info(text_map_logfont(lf))->face, "System") == 0);
  /* A registered FNT (here the synthetic one) is reachable by its face. */
  mk_logfont(lf, -3, 0, "TestFnt");
  CHECK_EQ(text_map_logfont(lf), g_tf_index);
  /* Rotated text (the game's lfEscapement 3150) is accepted; the font is not rotated. */
  mk_logfont(lf, -11, 0, "Arial");
  p16(lf + LF_ESCAPEMENT, 3150);
  FARPTR fp = far_alloc(LOGFONT16_SIZE);
  memcpy(LIN(fp), lf, sizeof lf);
  CHECK(CreateFontIndirect(fp) != 0);
  CHECK_EQ(CreateFontIndirect(0), 0);
  free_dc(dc, bm);
}

static void test_font_objects(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(8, 8, &bm);
  HGDIOBJ16 sys = GetStockObject(SYSTEM_FONT);
  CHECK(sys != 0);
  CHECK(GetStockObject(ANSI_VAR_FONT) != 0);
  CHECK(GetStockObject(OEM_FIXED_FONT) != 0);
  CHECK(GetStockObject(ANSI_FIXED_FONT) != 0);
  CHECK(GetStockObject(DEVICE_DEFAULT_FONT) != 0);
  CHECK(GetStockObject(SYSTEM_FIXED_FONT) != 0);
  CHECK_EQ(metric(dc, TM_HEIGHT), 16); /* a new DC has the System font */
  HFONT16 f = mk_font(-11, 0, "Arial");
  CHECK_EQ(SelectObject(dc, f), sys);
  CHECK_EQ(DeleteObject(f), 0);        /* selected: cannot be deleted */
  CHECK_EQ(SelectObject(dc, sys), f);
  CHECK_EQ(DeleteObject(f), 1);
  CHECK_EQ(DeleteObject(sys), 0);      /* stock: cannot be deleted */
  /* DeleteDC releases the font selected into the DC. */
  {
    HBITMAP16 bm2;
    HDC16 dc2 = mk_dc(8, 8, &bm2);
    HFONT16 f2 = mk_font(-11, 0, "Arial");
    SelectObject(dc2, f2);
    CHECK_EQ(DeleteObject(f2), 0);
    free_dc(dc2, bm2);
    CHECK_EQ(DeleteObject(f2), 1);
  }
  /* A deleted font handle is not a font. */
  CHECK_EQ(SelectObject(dc, f), 0);
  CHECK_EQ(metric(dc, TM_HEIGHT), 16);
  free_dc(dc, bm);
}

/* ---------------------------------------------------------------- harness thunks */

static uint32_t call_api(const char *mod, uint16_t ord, const uint16_t *args, int n) {
  uint32_t fp = harness_thunk_far(mod, ord);
  CHECK(fp != 0);
  if (!fp) return 0;
  return harness_call_far(fp, HCONV_PASCAL, args, n);
}

static void test_thunks(void) {
  HBITMAP16 bm;
  HDC16 dc = mk_dc(40, 10, &bm);
  uint8_t lf[LOGFONT16_SIZE];
  mk_logfont(lf, -3, 0, "TestFnt");
  FARPTR flf = far_alloc(LOGFONT16_SIZE);
  memcpy(LIN(flf), lf, sizeof lf);
  /* CreateFontIndirect (GDI.57): ptr = selector word first, then offset. */
  uint16_t a1[2] = {(uint16_t)(flf >> 16), (uint16_t)(flf & 0xFFFF)};
  HFONT16 hf = (HFONT16)call_api("GDI", 57, a1, 2);
  CHECK(hf != 0);
  SelectObject(dc, hf);
  SetBkMode(dc, TRANSPARENT);
  FARPTR s = far_str("Hello");
  /* GetTextExtent (GDI.91): hdc, str, count -> DX:AX = height:width. */
  uint16_t a2[4] = {dc, (uint16_t)(s >> 16), (uint16_t)(s & 0xFFFF), 5};
  uint32_t ext = call_api("GDI", 91, a2, 4);
  CHECK_EQ(ext & 0xFFFF, 12);
  CHECK_EQ(ext >> 16, 4);
  /* TextOut (GDI.33): hdc, x, y, str, count. */
  FARPTR sa = far_str("A");
  uint16_t a3[6] = {dc, 2, 1, (uint16_t)(sa >> 16), (uint16_t)(sa & 0xFFFF), 1};
  CHECK_EQ(call_api("GDI", 33, a3, 6) & 0xFFFF, 1);
  CHECK_EQ(pix(bm, 3, 1), BLACK_IDX);   /* A top middle at x = 2 + 1 */
  CHECK_EQ(pix(bm, 2, 1), WHITE_IDX);
  /* GetTextMetrics (GDI.93): hdc, ptr. */
  FARPTR tm = far_alloc(TEXTMETRIC16_SIZE);
  uint16_t a4[3] = {dc, (uint16_t)(tm >> 16), (uint16_t)(tm & 0xFFFF)};
  CHECK_EQ(call_api("GDI", 93, a4, 3) & 0xFFFF, 1);
  CHECK_EQ(((const uint8_t *)LIN(tm))[0], 4);
  /* ExtTextOut (GDI.351): hdc, x, y, options, rect, str, count, dx. */
  PatBlt(dc, 0, 0, 40, 10, WHITENESS);
  FARPTR rc = far_rect(0, 0, 3, 10);
  SetBkColor(dc, C_RED);
  uint16_t a5[11] = {dc, 1, 1, ETO_OPAQUE | ETO_CLIPPED, (uint16_t)(rc >> 16), (uint16_t)(rc & 0xFFFF),
                     (uint16_t)(sa >> 16), (uint16_t)(sa & 0xFFFF), 1, 0, 0};
  CHECK_EQ(call_api("GDI", 351, a5, 11) & 0xFFFF, 1);
  CHECK_EQ(pix(bm, 0, 9), RED_IDX);     /* the rectangle was filled */
  CHECK_EQ(pix(bm, 2, 1), BLACK_IDX);   /* A top middle at x = 1 + 1 */
  /* DrawText (USER.85): hdc, str, count, rect, flags; count -1 passes as 0xFFFF. */
  PatBlt(dc, 0, 0, 40, 10, WHITENESS);
  FARPTR rc2 = far_rect(0, 0, 40, 10);
  uint16_t a6[7] = {dc, (uint16_t)(sa >> 16), (uint16_t)(sa & 0xFFFF), 0xFFFF, (uint16_t)(rc2 >> 16),
                    (uint16_t)(rc2 & 0xFFFF), DT_SINGLELINE | DT_RIGHT};
  CHECK_EQ(call_api("USER", 85, a6, 7) & 0xFFFF, 4);
  CHECK_EQ(pix(bm, 38, 0), BLACK_IDX);  /* A is 3 wide, ends at 40: top middle at 38 */
  SelectObject(dc, GetStockObject(SYSTEM_FONT));
  CHECK_EQ(DeleteObject(hf), 1);
  free_dc(dc, bm);
}

/* ---------------------------------------------------------------- driver */

static void run_runtime_tests(void) {
  g_tf_index = text_register_font(g_tf3, g_tf3_size);
  CHECK(g_tf_index >= 3);
  test_fnt_decoder();
  test_builtin_fonts();
  test_textout_synthetic();
  test_textout_transparent_keeps_background();
  test_extent_and_metrics();
  test_textout_builtin_mask();
  test_exttextout();
  test_drawtext();
  test_drawtext_prefix();
  test_drawtext_lines();
  test_font_mapping();
  test_font_objects();
}

int main(void) {
  g_tf2_size = build_testfnt(g_tf2, sizeof g_tf2, 0x200);
  g_tf3_size = build_testfnt(g_tf3, sizeof g_tf3, 0x300);

  /* Phase 1: the runtime alone, no harness. */
  mem_init(ARENA_BYTES);
  CHECK_EQ(gdi_init(), 0);
  g_scr = gdi_create_screen_dc();
  run_runtime_tests();
  gdi_shutdown();
  mem_shutdown();

  /* Phase 2: the PASCAL thunks of harness/text_thunks.c under the harness. */
  harness_set_quiet(true);
  if (harness_load(EXE) != 0) {
    fprintf(stderr, "cannot load %s (run from the repo root)\n", EXE);
    return 1;
  }
  CHECK_EQ(gdi_init(), 0);
  gdi_register();
  g_tf_index = text_register_font(g_tf3, g_tf3_size);
  g_scr = gdi_create_screen_dc();
  test_thunks();
  gdi_shutdown();
  harness_shutdown();
  return TEST_DONE();
}
