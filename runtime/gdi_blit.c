/* GDI core part 2 (task stars-0z4.4): raster operations, PatBlt, BitBlt, StretchDIBits and
 * GetDIBits. ROP3 codes are evaluated from the operation-index byte (bits 16-23 of the code),
 * not from a table of named operations. Plain C: no harness dependency.
 *
 * Deviations from Windows 3.1, all documented in docs/inventory.md: StretchDIBits samples
 * (floor mapping) when shrinking instead of combining scan lines (STRETCH_ANDSCANS), negative
 * extents are rejected, RLE DIBs are rejected. */
#include <string.h>

#include "runtime/gdi_internal.h"

/* ---------------------------------------------------------------- evaluators */

uint8_t gdi_rop3(uint32_t rop, uint8_t p, uint8_t s, uint8_t d) {
  uint8_t idx = (uint8_t)(rop >> 16), r = 0;
  for (int k = 0; k < 8; k++) {
    if (!((idx >> k) & 1)) continue;
    uint8_t mp = (k & 4) ? p : (uint8_t)~p;
    uint8_t ms = (k & 2) ? s : (uint8_t)~s;
    uint8_t md = (k & 1) ? d : (uint8_t)~d;
    r |= (uint8_t)(mp & ms & md);
  }
  return r;
}

/* R2_* n (1..16) has the truth table n - 1 over (P << 1) | D. */
uint8_t gdi_rop2(int n, uint8_t p, uint8_t d) {
  if (n < R2_BLACK || n > R2_WHITE) n = R2_COPYPEN;
  int tt = n - 1;
  uint8_t r = 0;
  for (int k = 0; k < 4; k++) {
    if (!((tt >> k) & 1)) continue;
    uint8_t mp = (k & 2) ? p : (uint8_t)~p;
    uint8_t md = (k & 1) ? d : (uint8_t)~d;
    r |= (uint8_t)(mp & md);
  }
  return r;
}

uint8_t gdi_apply_rop2(int n, uint8_t p, uint8_t d) { return gdi_rop2(n, p, d); }

/* Does the operation depend on the source (m = 2) or on the pattern (m = 4)? */
static bool rop_uses(uint8_t idx, int m) {
  for (int k = 0; k < 8; k++)
    if (((idx >> k) & 1) != ((idx >> (k ^ m)) & 1)) return true;
  return false;
}

/* ---------------------------------------------------------------- pixel pipeline */

typedef struct Dest {
  DCObj *dc;
  Surf s;
  uint8_t idx;     /* ROP3 index byte */
  bool uses_pat;
  uint8_t pat[64];
  uint8_t mask;
} Dest;

/* Prepares the destination; false when the DC is invalid or a pattern ROP has no brush data. */
static bool dest_prepare(Dest *d, HDC16 hdc, uint32_t rop) {
  d->dc = gdi_dc_get(hdc);
  if (!d->dc || !dc_surface(d->dc, &d->s)) return false;
  d->idx = (uint8_t)(rop >> 16);
  d->uses_pat = rop_uses(d->idx, 4);
  d->mask = d->s.mono ? 1 : 0xFF;
  if (d->uses_pat) {
    BrushObj *b = gdi_obj_get(d->dc->hbrush, GDI_OBJ_BRUSH);
    if (!b || b->style == BS_HOLLOW) return false; /* a hollow brush draws nothing */
    gdi_brush_pattern(d->dc, b, d->pat);
  }
  return true;
}

static void dest_pixel(Dest *d, int32_t sx, int32_t sy, uint8_t src) {
  if (!gdi_visible(d->dc, &d->s, sx, sy)) return;
  uint8_t p = 0;
  if (d->uses_pat) {
    int px = (int)((sx - d->dc->vox - d->dc->box) & 7), py = (int)((sy - d->dc->voy - d->dc->boy) & 7);
    p = d->pat[py * 8 + px];
  }
  uint8_t *q = &d->s.p[sy * d->s.w + sx];
  *q = (uint8_t)(gdi_rop3((uint32_t)d->idx << 16, p, src, *q) & d->mask);
}

uint16_t PatBlt(HDC16 hdc, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rop) {
  Dest d;
  if (!gdi_dc_get(hdc)) return 0;
  if (!dest_prepare(&d, hdc, rop)) return gdi_dc_get(hdc) ? 1 : 0; /* hollow brush: nothing to draw */
  int32_t x0 = x, y0 = y, ww = w, hh = h;
  if (ww < 0) { x0 += ww; ww = -ww; }
  if (hh < 0) { y0 += hh; hh = -hh; }
  int32_t sx0 = gdi_sx(d.dc, x0), sy0 = gdi_sy(d.dc, y0);
  int32_t xa = sx0 < 0 ? 0 : sx0, ya = sy0 < 0 ? 0 : sy0;
  int32_t xb = sx0 + ww > d.s.w ? d.s.w : sx0 + ww, yb = sy0 + hh > d.s.h ? d.s.h : sy0 + hh;
  for (int32_t yy = ya; yy < yb; yy++)
    for (int32_t xx = xa; xx < xb; xx++) dest_pixel(&d, xx, yy, 0);
  return 1;
}

uint16_t BitBlt(HDC16 hdcDest, int16_t x, int16_t y, int16_t w, int16_t h, HDC16 hdcSrc, int16_t xs,
                int16_t ys, uint32_t rop) {
  Dest d;
  if (!gdi_dc_get(hdcDest)) return 0;
  uint8_t idx = (uint8_t)(rop >> 16);
  bool uses_s = rop_uses(idx, 2);
  DCObj *sdc = hdcSrc ? gdi_dc_get(hdcSrc) : NULL;
  Surf ss;
  if (uses_s && (!sdc || !dc_surface(sdc, &ss))) return 0;
  if (!dest_prepare(&d, hdcDest, rop)) return 1;
  if (w <= 0 || h <= 0) return 1;

  int32_t dx0 = gdi_sx(d.dc, x), dy0 = gdi_sy(d.dc, y);
  int32_t sx0 = 0, sy0 = 0;
  uint8_t tidx = 0, bidx = 0, src_bk = 0;
  if (uses_s) {
    sx0 = gdi_sx(sdc, xs);
    sy0 = gdi_sy(sdc, ys);
    tidx = gdi_pen_index(d.dc, d.dc->textcolor);
    bidx = gdi_pen_index(d.dc, d.dc->bkcolor);
    src_bk = gdi_pen_index(sdc, sdc->bkcolor);
  }
  uint16_t *row = rt_alloc((size_t)w * sizeof(uint16_t));
  if (!row) return 0;
  bool same = uses_s && ss.p == d.s.p;
  bool bottom_up = same && dy0 > sy0;
  for (int32_t r = 0; r < h; r++) {
    int32_t rr = bottom_up ? h - 1 - r : r;
    int32_t dyy = dy0 + rr;
    if (dyy < 0 || dyy >= d.s.h) continue;
    /* Fetch the whole source row first (so same-surface overlap in x is safe). */
    for (int32_t c = 0; c < w; c++) {
      if (!uses_s) { row[c] = 0; continue; }
      int32_t px = sx0 + c, py = sy0 + rr;
      if (px < 0 || py < 0 || px >= ss.w || py >= ss.h) { row[c] = 0x100; continue; }
      uint8_t v = ss.p[py * ss.w + px];
      if (ss.mono && !d.s.mono) v = v ? bidx : tidx;      /* 0 bits text colour, 1 bits background */
      else if (!ss.mono && d.s.mono) v = (v == src_bk) ? 1 : 0;
      row[c] = v;
    }
    for (int32_t c = 0; c < w; c++)
      if (row[c] != 0x100) dest_pixel(&d, dx0 + c, dyy, (uint8_t)row[c]);
  }
  rt_free(row);
  return 1;
}

/* ---------------------------------------------------------------- DIBs */

static uint32_t rd16(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }
static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

typedef struct DibInfo {
  int32_t w, h;      /* h > 0 */
  bool bottom_up;
  int bpp;
  uint32_t size;     /* header size */
  uint32_t entry;    /* colour table entry size in bytes (4 RGBQUAD, 3 RGBTRIPLE) */
  uint32_t ncolors;
  uint32_t stride;
} DibInfo;

static bool dib_parse(FARPTR bmi, DibInfo *di) {
  if (!bmi || !mem_check(bmi, 12)) return false;
  const uint8_t *p = LIN(bmi);
  uint32_t sz = rd32(p);
  if (sz == 12) {
    di->w = (int32_t)rd16(p + 4);
    di->h = (int32_t)rd16(p + 6);
    di->bpp = (int)rd16(p + 10);
    di->entry = 3;
    di->bottom_up = true;
    di->size = 12;
  } else if (sz >= 40) {
    if (!mem_check(bmi, 40)) return false;
    di->w = (int32_t)rd32(p + 4);
    int32_t h = (int32_t)rd32(p + 8);
    di->bottom_up = h > 0;
    di->h = h < 0 ? -h : h;
    di->bpp = (int)rd16(p + 14);
    if (rd32(p + 16) != 0) return false; /* BI_RLE8, BI_RLE4, BI_BITFIELDS */
    di->entry = 4;
    di->size = sz;
  } else {
    return false;
  }
  if (di->w <= 0 || di->h <= 0 || di->w > 32767 || di->h > 32767) return false;
  if (di->bpp != 1 && di->bpp != 4 && di->bpp != 8 && di->bpp != 24) return false;
  di->ncolors = 0;
  if (di->bpp <= 8) {
    di->ncolors = 1u << di->bpp;
    if (sz >= 40) {
      uint32_t used = rd32(p + 32);
      if (used && used < di->ncolors) di->ncolors = used;
    }
  }
  di->stride = (((uint32_t)di->w * (uint32_t)di->bpp + 31) / 32) * 4;
  return true;
}

int16_t StretchDIBits(HDC16 hdc, int16_t xd, int16_t yd, int16_t cxd, int16_t cyd, int16_t xs,
                      int16_t ys, int16_t cxs, int16_t cys, FARPTR bits, FARPTR bmi, uint16_t usage,
                      uint32_t rop) {
  Dest d;
  DibInfo di;
  if (!gdi_dc_get(hdc) || !dib_parse(bmi, &di) || !bits) return 0;
  if (cxd <= 0 || cyd <= 0 || cxs <= 0 || cys <= 0) return 0;
  uint32_t table_bytes = di.ncolors * (usage == DIB_PAL_COLORS ? 2u : di.entry);
  if (!mem_check(bmi, di.size + table_bytes) || !mem_check(bits, di.stride * (uint32_t)di.h)) return 0;
  if (!dest_prepare(&d, hdc, rop)) return (int16_t)cys;

  /* Colour lookup table for palettised DIBs: DIB index -> destination pixel value. */
  uint8_t lut[256];
  const uint8_t *tab = (const uint8_t *)LIN(bmi) + di.size;
  PaletteObj *pal = gdi_palette_get(d.dc->hpal);
  for (uint32_t i = 0; i < di.ncolors; i++) {
    if (usage == DIB_PAL_COLORS) {
      unsigned li = rd16(tab + 2 * i);
      COLORREF16 c = GDI_PALETTEINDEX(li);
      lut[i] = pal && li < pal->n ? gdi_pen_index(d.dc, c) : 0;
    } else {
      const uint8_t *e = tab + di.entry * i; /* B, G, R */
      lut[i] = gdi_dib_index(d.dc, e[2], e[1], e[0]);
    }
  }
  const uint8_t *bp = LIN(bits);
  /* Visible top row of the source rectangle within the stored rows. */
  int32_t vis_top = di.bottom_up ? di.h - ys - cys : ys;
  int32_t dx0 = gdi_sx(d.dc, xd), dy0 = gdi_sy(d.dc, yd);
  for (int32_t r = 0; r < cyd; r++) {
    int32_t dyy = dy0 + r;
    if (dyy < 0 || dyy >= d.s.h) continue;
    int32_t v = vis_top + (int32_t)(((int64_t)r * cys) / cyd);
    if (v < 0 || v >= di.h) continue;
    const uint8_t *rowp = bp + (uint32_t)(di.bottom_up ? di.h - 1 - v : v) * di.stride;
    for (int32_t c = 0; c < cxd; c++) {
      int32_t sxp = xs + (int32_t)(((int64_t)c * cxs) / cxd);
      if (sxp < 0 || sxp >= di.w) continue;
      uint8_t pv;
      switch (di.bpp) {
        case 1: pv = lut[(rowp[sxp >> 3] >> (7 - (sxp & 7))) & 1]; break;
        case 4: pv = lut[(sxp & 1) ? (rowp[sxp >> 1] & 15) : (rowp[sxp >> 1] >> 4)]; break;
        case 8: pv = lut[rowp[sxp]]; break;
        default: {
          const uint8_t *q = rowp + 3 * sxp;
          pv = gdi_dib_index(d.dc, q[2], q[1], q[0]);
        }
      }
      dest_pixel(&d, dx0 + c, dyy, pv);
    }
  }
  return cys;
}

static const uint8_t k_vga16[16][3] = {{0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
                                       {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
                                       {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
                                       {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};

static int nearest_in(const uint8_t (*t)[3], int n, int r, int g, int b) {
  long best = -1;
  int bi = 0;
  for (int i = 0; i < n; i++) {
    long dr = t[i][0] - r, dg = t[i][1] - g, db = t[i][2] - b;
    long dd = dr * dr + dg * dg + db * db;
    if (best < 0 || dd < best) { best = dd; bi = i; }
  }
  return bi;
}

int16_t GetDIBits(HDC16 hdc, HBITMAP16 hbm, uint16_t start, uint16_t num, FARPTR bits, FARPTR bmi,
                  uint16_t usage) {
  GdiState *g = gdi_state();
  BitmapObj *bm = gdi_obj_get(hbm, GDI_OBJ_BITMAP);
  if (!g || !gdi_dc_get(hdc) || !bm || !bmi || !mem_check(bmi, 40)) return 0;
  uint8_t *hd = LIN(bmi);
  if (rd32(hd) != 40) return 0; /* BITMAPCOREHEADER is not supported here */
  const uint8_t *src = (const uint8_t *)(bm + 1);
  int bpp = bm->mono ? 1 : 8;
  if (bits) {
    bpp = (int)rd16(hd + 14);
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24) return 0;
  } else {
    /* Header-only call: describe the bitmap. */
    wr32(hd + 4, (uint32_t)bm->w);
    wr32(hd + 8, (uint32_t)bm->h);
    wr16(hd + 12, 1);
    wr16(hd + 14, (uint32_t)bpp);
    wr32(hd + 16, 0);
    wr32(hd + 20, ((((uint32_t)bm->w * (uint32_t)bpp + 31) / 32) * 4) * (uint32_t)bm->h);
    wr32(hd + 32, 0);
    wr32(hd + 36, 0);
  }
  uint32_t ncol = bpp <= 8 ? 1u << bpp : 0;
  uint32_t stride = (((uint32_t)bm->w * (uint32_t)bpp + 31) / 32) * 4;
  uint32_t tab_bytes = ncol * (usage == DIB_PAL_COLORS ? 2u : 4u);
  if (!mem_check(bmi, 40 + tab_bytes)) return 0;
  uint8_t *tab = hd + 40;
  /* Colour table: system palette (8 bpp), the 16 VGA colours (4 bpp) or black and white. */
  uint8_t trgb[256][3];
  for (uint32_t i = 0; i < ncol; i++) {
    if (bpp == 8) memcpy(trgb[i], g->sys_rgb[i], 3);
    else if (bpp == 4) memcpy(trgb[i], k_vga16[i], 3);
    else memset(trgb[i], i ? 255 : 0, 3);
    if (usage == DIB_PAL_COLORS) wr16(tab + 2 * i, i);
    else {
      tab[4 * i] = trgb[i][2];
      tab[4 * i + 1] = trgb[i][1];
      tab[4 * i + 2] = trgb[i][0];
      tab[4 * i + 3] = 0;
    }
  }
  if (!bits) return 1;
  if (start >= (uint32_t)bm->h) return 0;
  uint32_t rows = num;
  if (start + rows > (uint32_t)bm->h) rows = (uint32_t)bm->h - start;
  if (!mem_check(bits, stride * rows)) return 0;
  uint8_t *out = LIN(bits);
  memset(out, 0, stride * rows);
  for (uint32_t r = 0; r < rows; r++) {
    int32_t srow = bm->h - 1 - (int32_t)(start + r); /* scan line 0 is the bottom row */
    uint8_t *o = out + r * stride;
    for (int32_t x = 0; x < bm->w; x++) {
      uint8_t v = src[srow * bm->w + x];
      int rgb[3];
      if (bm->mono) rgb[0] = rgb[1] = rgb[2] = v ? 255 : 0;
      else { rgb[0] = g->sys_rgb[v][0]; rgb[1] = g->sys_rgb[v][1]; rgb[2] = g->sys_rgb[v][2]; }
      uint32_t q;
      if (bpp == 24) {
        o[3 * x] = (uint8_t)rgb[2]; o[3 * x + 1] = (uint8_t)rgb[1]; o[3 * x + 2] = (uint8_t)rgb[0];
        continue;
      }
      if (bpp == 8 && !bm->mono) q = v;
      else q = (uint32_t)nearest_in((const uint8_t(*)[3])trgb, (int)ncol, rgb[0], rgb[1], rgb[2]);
      if (bpp == 8) o[x] = (uint8_t)q;
      else if (bpp == 4) o[x >> 1] = (uint8_t)((x & 1) ? (o[x >> 1] | q) : (q << 4));
      else o[x >> 3] = (uint8_t)(o[x >> 3] | (q << (7 - (x & 7))));
    }
  }
  return (int16_t)rows;
}
