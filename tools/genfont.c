/* genfont: build-time rasteriser that turns the OFL-licensed Liberation Sans outlines into the
 * Windows FNT 3.0 raster fonts compiled into runtime/fontdata.c (task stars-0z4.5, owner
 * amendment 2026-10-17: permissively licensed look-alike fonts, no Microsoft font files).
 *
 *   cc tools/genfont.c -I$(brew --prefix freetype)/include/freetype2 \
 *      -L$(brew --prefix freetype)/lib -lfreetype -o /tmp/genfont
 *   /tmp/genfont LiberationSans-Regular.ttf LiberationSans-Bold.ttf > runtime/fontdata.c
 *
 * Rasteriser: FreeType (FTL/GPL dual licence; used only at generation time, not linked into the
 * runtime) with the v35 TrueType bytecode interpreter, FT_LOAD_TARGET_MONO (1-bit, hinted, no
 * anti-aliasing). Output is deterministic for a given FreeType version; the version used is
 * written into the generated file header. The result is a look-alike, NOT a copy of MS Sans
 * Serif or System: glyph shapes and widths differ from Windows 3.x (UNVERIFIED against it).
 *
 * Fonts emitted (index order is the registry order of runtime/text.c):
 *   0 "System"        Sans Bold    em 13 px, cell 16, ascent 13  (stand-in for the Win3.1 System font)
 *   1 "MS Sans Serif" Sans Regular em 11 px, cell 13, ascent 11  (8 pt at 96 dpi)
 *   then "Arial" regular and bold at em 8, 9, 10, 11, 13, 16, 19, 24, 32 px.
 */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST 32
#define LAST 255
#define NCH (LAST - FIRST + 1)

typedef struct {
  const char *face;
  int bold;
  int em, cell, ascent;
  int is_system; /* mark with the SWISS family either way */
} Spec;

typedef struct {
  int w, h;
  uint8_t *bits; /* h rows of w bytes, 0/1 */
} Glyph;

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, unsigned v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

static unsigned cp1252_to_unicode(int c) {
  /* 0x80-0x9f of Windows ANSI are punctuation; anything else is Latin-1. */
  static const unsigned t[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                 0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013,
                                 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
  return (c >= 0x80 && c < 0xa0) ? t[c - 0x80] : (unsigned)c;
}

static void emit(FT_Library lib, FT_Face face, const Spec *s, const char *name, int idx, int fam_swiss) {
  (void)lib;
  FT_Set_Pixel_Sizes(face, 0, (FT_UInt)s->em);
  Glyph g[NCH];
  int maxw = 0;
  long wsum = 0;
  for (int c = FIRST; c <= LAST; c++) {
    Glyph *gl = &g[c - FIRST];
    gl->h = s->cell;
    gl->w = 0;
    gl->bits = NULL;
    unsigned u = cp1252_to_unicode(c);
    FT_UInt gi = u ? FT_Get_Char_Index(face, u) : 0;
    if (!gi || FT_Load_Glyph(face, gi, FT_LOAD_TARGET_MONO | FT_LOAD_RENDER)) {
      gl->w = (c == ' ') ? (s->em + 2) / 3 : 0;
      if (c >= 0xa0 || c == ' ') gl->w = gl->w ? gl->w : (s->em + 2) / 3;
      if (gl->w == 0) gl->w = (s->em + 1) / 2; /* undefined slot: empty box width */
      gl->bits = calloc((size_t)gl->w * (size_t)gl->h, 1);
    } else {
      FT_GlyphSlot sl = face->glyph;
      int adv = (int)((sl->advance.x + 32) >> 6);
      int left = sl->bitmap_left, bw = (int)sl->bitmap.width, bh = (int)sl->bitmap.rows;
      int w = adv;
      if (left < 0) left = 0;
      if (left + bw > w) w = left + bw;
      if (w < 1) w = 1;
      gl->w = w;
      gl->bits = calloc((size_t)w * (size_t)gl->h, 1);
      int top = s->ascent - sl->bitmap_top;
      for (int y = 0; y < bh; y++)
        for (int x = 0; x < bw; x++) {
          int px = left + x, py = top + y;
          if (py < 0 || py >= gl->h) continue;
          if (sl->bitmap.buffer[y * sl->bitmap.pitch + (x >> 3)] & (0x80 >> (x & 7))) gl->bits[py * w + px] = 1;
        }
    }
    if (gl->w > maxw) maxw = gl->w;
    wsum += gl->w;
  }
  /* layout: header 148, glyph table (NCH + 1) * 6, bitmaps, face name */
  unsigned hdr = 148, tbl = hdr, bits_off = tbl + (NCH + 1) * 6;
  unsigned total = bits_off;
  for (int i = 0; i < NCH; i++) total += (unsigned)((g[i].w + 7) / 8) * (unsigned)g[i].h;
  unsigned face_off = total;
  total += (unsigned)strlen(name) + 1;
  uint8_t *b = calloc(total, 1);
  put16(b + 0, 0x0300);
  put32(b + 2, total);
  snprintf((char *)b + 6, 60, "Look-alike raster (OFL Liberation Sans)");
  put16(b + 66, 0);                                     /* dfType: raster, bits inline */
  put16(b + 68, (unsigned)((s->em * 72 + 48) / 96));    /* dfPoints */
  put16(b + 70, 96);
  put16(b + 72, 96);
  put16(b + 74, (unsigned)s->ascent);
  put16(b + 76, (unsigned)(s->cell - s->em));           /* dfInternalLeading */
  put16(b + 78, 0);                                     /* dfExternalLeading */
  b[80] = b[81] = b[82] = 0;
  put16(b + 83, s->bold ? 700 : 400);
  b[85] = 0; /* ANSI_CHARSET */
  put16(b + 86, 0);
  put16(b + 88, (unsigned)s->cell);
  b[90] = (uint8_t)(0x01 | (fam_swiss ? 0x20 : 0)); /* variable pitch, FF_SWISS */
  put16(b + 91, (unsigned)(wsum / NCH));
  put16(b + 93, (unsigned)maxw);
  b[95] = FIRST;
  b[96] = LAST;
  b[97] = (uint8_t)('?' - FIRST);
  b[98] = (uint8_t)(' ' - FIRST);
  put16(b + 99, (unsigned)((wsum + 7) / 8));
  put32(b + 101, 0);
  put32(b + 105, face_off);
  put32(b + 109, 0);
  put32(b + 113, bits_off);
  b[117] = 0;
  put32(b + 118, 0x2); /* dfFlags: proportional */
  unsigned off = bits_off;
  for (int i = 0; i < NCH; i++) {
    put16(b + tbl + 6 * i, (unsigned)g[i].w);
    put32(b + tbl + 6 * i + 2, off);
    int ns = (g[i].w + 7) / 8;
    for (int st = 0; st < ns; st++)
      for (int y = 0; y < g[i].h; y++) {
        uint8_t v = 0;
        for (int k = 0; k < 8; k++) {
          int x = st * 8 + k;
          if (x < g[i].w && g[i].bits[y * g[i].w + x]) v |= (uint8_t)(0x80 >> k);
        }
        b[off++] = v;
      }
    free(g[i].bits);
  }
  put16(b + tbl + 6 * NCH, 0);
  put32(b + tbl + 6 * NCH + 2, off);
  memcpy(b + face_off, name, strlen(name) + 1);
  printf("static const uint8_t fnt_%d[%u] = {", idx, total);
  for (unsigned i = 0; i < total; i++) printf("%s%u,", (i % 24) ? "" : "\n", b[i]);
  printf("\n};\n");
  free(b);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: genfont LiberationSans-Regular.ttf LiberationSans-Bold.ttf > runtime/fontdata.c\n");
    return 2;
  }
  FT_Library lib;
  FT_Face reg, bold;
  FT_Init_FreeType(&lib);
  int ver = 35;
  FT_Property_Set(lib, "truetype", "interpreter-version", &ver);
  if (FT_New_Face(lib, argv[1], 0, &reg) || FT_New_Face(lib, argv[2], 0, &bold)) {
    fprintf(stderr, "cannot open fonts\n");
    return 1;
  }
  FT_Int mj, mn, pt;
  FT_Library_Version(lib, &mj, &mn, &pt);
  printf("/* GENERATED by tools/genfont.c (FreeType %d.%d.%d) from Liberation Sans 2.1.5 (SIL OFL 1.1,\n"
         " * third_party/fonts/README.md). Do not edit. Windows FNT 3.0 raster fonts; look-alikes, not\n"
         " * copies of any Microsoft font. */\n#include \"runtime/fontdata.h\"\n\n",
         mj, mn, pt);
  static const int ems[] = {8, 9, 10, 11, 13, 16, 19, 24, 32};
  Spec specs[2 + 2 * 9];
  const char *names[2 + 2 * 9];
  FT_Face faces[2 + 2 * 9];
  int n = 0;
  specs[n] = (Spec){"System", 1, 13, 16, 13, 1}; names[n] = "System"; faces[n++] = bold;
  specs[n] = (Spec){"MS Sans Serif", 0, 11, 13, 11, 0}; names[n] = "MS Sans Serif"; faces[n++] = reg;
  for (int bd = 0; bd < 2; bd++)
    for (unsigned i = 0; i < sizeof ems / sizeof ems[0]; i++) {
      int em = ems[i];
      int asc = (em * 905 + 500) / 1000, desc = (em * 212 + 500) / 1000;
      specs[n] = (Spec){"Arial", bd, em, asc + desc, asc, 0};
      names[n] = "Arial";
      faces[n++] = bd ? bold : reg;
    }
  for (int i = 0; i < n; i++) emit(lib, faces[i], &specs[i], names[i], i, 1);
  printf("\nconst FontBlob fontdata_blobs[] = {\n");
  for (int i = 0; i < n; i++) printf("    {fnt_%d, sizeof fnt_%d},\n", i, i);
  printf("};\nconst unsigned fontdata_count = %d;\n", n);
  return 0;
}
