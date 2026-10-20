/* Windows FNT 2.0 / 3.0 decoder (runtime/fnt.h). */
#include <string.h>

#include "runtime/fnt.h"

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16); }

int fnt_parse(const uint8_t *d, size_t size, FntFont *f) {
  if (!d || !f || size < 118) return -1;
  memset(f, 0, sizeof *f);
  f->data = d;
  f->size = size;
  f->version = rd16(d);
  unsigned hdr;
  if (f->version == 0x0200) {
    hdr = 118;
    f->entry_size = 4;
  } else if (f->version == 0x0300) {
    hdr = 148;
    f->entry_size = 6;
  } else {
    return -2;
  }
  if (size < hdr) return -1;
  f->type = rd16(d + 66);
  if (f->type & 1) return -3; /* vector font */
  f->points = rd16(d + 68);
  f->vert_res = rd16(d + 70);
  f->horiz_res = rd16(d + 72);
  f->ascent = rd16(d + 74);
  f->internal_leading = rd16(d + 76);
  f->external_leading = rd16(d + 78);
  f->italic = d[80];
  f->underline = d[81];
  f->strikeout = d[82];
  f->weight = rd16(d + 83);
  f->charset = d[85];
  f->pix_width = rd16(d + 86);
  f->pix_height = rd16(d + 88);
  f->pitch_family = d[90];
  f->avg_width = rd16(d + 91);
  f->max_width = rd16(d + 93);
  f->first = d[95];
  f->last = d[96];
  f->default_char = (uint8_t)(f->first + d[97]);
  f->break_char = (uint8_t)(f->first + d[98]);
  if (f->last < f->first || f->pix_height == 0) return -4;
  uint32_t face_off = rd32(d + 105);
  if (face_off && face_off < size) {
    size_t n = 0;
    while (face_off + n < size && d[face_off + n] && n < FNT_FACE_MAX - 1) {
      f->face[n] = (char)d[face_off + n];
      n++;
    }
    f->face[n] = 0;
  }
  f->table_off = hdr;
  unsigned nglyphs = (unsigned)(f->last - f->first) + 1;
  if ((size_t)hdr + (size_t)(nglyphs + 1) * f->entry_size > size) return -5;
  for (unsigned i = 0; i < nglyphs; i++) {
    const uint8_t *e = d + hdr + (size_t)i * f->entry_size;
    unsigned w = rd16(e);
    uint32_t off = f->entry_size == 4 ? rd16(e + 2) : rd32(e + 2);
    size_t need = (size_t)((w + 7) / 8) * f->pix_height;
    if (w && (off > size || need > size - off)) return -6;
  }
  return 0;
}

bool fnt_has_glyph(const FntFont *f, int ch) { return ch >= f->first && ch <= f->last; }

static const uint8_t *entry(const FntFont *f, int ch) {
  if (!fnt_has_glyph(f, ch)) ch = f->default_char;
  if (!fnt_has_glyph(f, ch)) return NULL;
  return f->data + f->table_off + (size_t)(ch - f->first) * f->entry_size;
}

int fnt_glyph_width(const FntFont *f, int ch) {
  const uint8_t *e = entry(f, ch);
  return e ? rd16(e) : 0;
}

bool fnt_glyph_pixel(const FntFont *f, int ch, int x, int y) {
  const uint8_t *e = entry(f, ch);
  if (!e) return false;
  int w = rd16(e);
  if (x < 0 || y < 0 || x >= w || y >= f->pix_height) return false;
  uint32_t off = f->entry_size == 4 ? rd16(e + 2) : rd32(e + 2);
  size_t at = (size_t)off + (size_t)(x >> 3) * f->pix_height + (size_t)y;
  return (f->data[at] & (0x80 >> (x & 7))) != 0;
}
