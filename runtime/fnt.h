/* Windows FNT 2.0 / 3.0 raster font decoder (task stars-0z4.5). A FNT is the body of a type-8
 * (RT_FONT) resource of a Windows 3.x .FON file. fnt_parse reads the header and validates the glyph
 * table; the FntFont keeps a host pointer into the caller's buffer, so it is runtime-private state
 * that is rebuilt from const data, never stored in the arena.
 *
 * Layout used (dfVersion 0x0200: 118-byte header, 4-byte glyph entries {width u16, offset u16};
 * 0x0300: 148-byte header, 6-byte entries {width u16, offset u32}). Offsets are from the start of
 * the resource. A glyph bitmap is ceil(width / 8) column strips of pixHeight bytes each, top to
 * bottom, bit 7 = leftmost pixel of the strip. dfDefaultChar and dfBreakChar are relative to
 * dfFirstChar. Written from the Windows 3.1 DDK font file format description (from memory,
 * UNVERIFIED against a real .FON). */
#ifndef STARS_RUNTIME_FNT_H
#define STARS_RUNTIME_FNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FNT_FACE_MAX 32

typedef struct FntFont {
  const uint8_t *data;
  size_t size;
  uint16_t version; /* 0x0200 or 0x0300 */
  uint16_t type, points, vert_res, horiz_res;
  uint16_t ascent, internal_leading, external_leading;
  uint8_t italic, underline, strikeout;
  uint16_t weight;
  uint8_t charset;
  uint16_t pix_width, pix_height;
  uint8_t pitch_family;
  uint16_t avg_width, max_width;
  uint8_t first, last, default_char, break_char; /* default_char / break_char are absolute here */
  char face[FNT_FACE_MAX];
  uint32_t table_off;     /* glyph table */
  unsigned entry_size;    /* 4 or 6 */
} FntFont;

/* 0 on success, negative when the resource is malformed (bad version, truncated, offsets out of
 * range, glyph bitmaps outside the buffer). */
int fnt_parse(const uint8_t *data, size_t size, FntFont *out);

/* True when ch is in [first, last]. */
bool fnt_has_glyph(const FntFont *f, int ch);
/* Advance width of ch; characters outside the font use the default character. */
int fnt_glyph_width(const FntFont *f, int ch);
/* Pixel (x, y) of the glyph; false outside the glyph box. Characters outside the font use the
 * default character. */
bool fnt_glyph_pixel(const FntFont *f, int ch, int x, int y);

#endif
