/* Clip regions as rectangle lists (task stars-0z4.4). A region is a list of disjoint
 * rectangles; ExcludeClipRect splits them. DC clip regions are in DC device coordinates
 * (logical minus window origin). Plain C: no harness dependency. */
#include <string.h>

#include "runtime/gdi_internal.h"

typedef struct RL {
  RectI r[GDI_MAX_CLIP_RECTS];
  int n;
} RL;

static RegionObj *clip_block(const DCObj *dc) {
  return dc->clip_off ? (RegionObj *)(mem_arena() + dc->clip_off) : NULL;
}

static RectI *rects_of(RegionObj *rg) { return (RectI *)(rg + 1); }

bool gdi_clip_contains(const DCObj *dc, int32_t x, int32_t y) {
  RegionObj *rg = clip_block(dc);
  if (!rg) return true;
  const RectI *r = rects_of(rg);
  for (int i = 0; i < rg->count; i++)
    if (x >= r[i].l && x < r[i].r && y >= r[i].t && y < r[i].b) return true;
  return false;
}

bool gdi_visible(const DCObj *dc, const Surf *s, int32_t sx, int32_t sy) {
  if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) return false;
  if (!dc->memory && (sx < dc->hard.l || sx >= dc->hard.r || sy < dc->hard.t || sy >= dc->hard.b)) return false;
  return gdi_clip_contains(dc, sx - dc->vox, sy - dc->voy);
}

static void rl_load(const DCObj *dc, RL *out) {
  RegionObj *rg = clip_block(dc);
  if (!rg) {
    out->n = 1;
    out->r[0] = (RectI){-GDI_BIG, -GDI_BIG, GDI_BIG, GDI_BIG};
    return;
  }
  out->n = rg->count;
  memcpy(out->r, rects_of(rg), (size_t)rg->count * sizeof(RectI));
}

static int32_t imax(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t imin(int32_t a, int32_t b) { return a < b ? a : b; }

/* in minus hole -> out. false on overflow. */
static bool rl_subtract(RL *out, const RL *in, RectI hole) {
  out->n = 0;
  for (int i = 0; i < in->n; i++) {
    RectI a = in->r[i];
    RectI x = {imax(a.l, hole.l), imax(a.t, hole.t), imin(a.r, hole.r), imin(a.b, hole.b)};
    RectI piece[4];
    int np = 0;
    if (x.l >= x.r || x.t >= x.b) {
      piece[np++] = a; /* no overlap */
    } else {
      if (a.t < x.t) piece[np++] = (RectI){a.l, a.t, a.r, x.t};   /* above */
      if (x.b < a.b) piece[np++] = (RectI){a.l, x.b, a.r, a.b};   /* below */
      if (a.l < x.l) piece[np++] = (RectI){a.l, x.t, x.l, x.b};   /* left */
      if (x.r < a.r) piece[np++] = (RectI){x.r, x.t, a.r, x.b};   /* right */
    }
    for (int k = 0; k < np; k++) {
      if (out->n >= GDI_MAX_CLIP_RECTS) return false;
      out->r[out->n++] = piece[k];
    }
  }
  return true;
}

static void rl_intersect(RL *out, const RL *in, RectI c) {
  out->n = 0;
  for (int i = 0; i < in->n; i++) {
    RectI x = {imax(in->r[i].l, c.l), imax(in->r[i].t, c.t), imin(in->r[i].r, c.r), imin(in->r[i].b, c.b)};
    if (x.l < x.r && x.t < x.b) out->r[out->n++] = x;
  }
}

static int16_t kind_of(int n) { return n == 0 ? NULLREGION : n == 1 ? SIMPLEREGION : COMPLEXREGION; }

/* With no clip set the region is the surface (item 5a): the stored rectangle list may still
 * extend past the surface ("no clip" is infinite), so the region kind counts only the part of
 * the list inside the surface bounds. */
static int16_t kind_in_surface(const DCObj *dc, const RL *l) {
  Surf s;
  if (!dc_surface(dc, &s)) return kind_of(l->n);
  RectI b = {-dc->vox, -dc->voy, s.w - dc->vox, s.h - dc->voy};
  if (!dc->memory) {
    if (dc->hard.l - dc->vox > b.l) b.l = dc->hard.l - dc->vox;
    if (dc->hard.t - dc->voy > b.t) b.t = dc->hard.t - dc->voy;
    if (dc->hard.r - dc->vox < b.r) b.r = dc->hard.r - dc->vox;
    if (dc->hard.b - dc->voy < b.b) b.b = dc->hard.b - dc->voy;
  }
  int n = 0;
  for (int i = 0; i < l->n; i++)
    if (imax(l->r[i].l, b.l) < imin(l->r[i].r, b.r) && imax(l->r[i].t, b.t) < imin(l->r[i].b, b.b)) n++;
  return kind_of(n);
}

static bool rl_store(DCObj *dc, const RL *l) {
  RegionObj *nb = rt_alloc(sizeof(RegionObj) + (size_t)l->n * sizeof(RectI));
  if (!nb) return false;
  nb->count = l->n;
  if (l->n) memcpy(rects_of(nb), l->r, (size_t)l->n * sizeof(RectI));
  RegionObj *old = clip_block(dc);
  if (old) rt_free(old);
  dc->clip_off = mem_off(nb);
  return true;
}

/* Logical rect -> DC device coordinates (normalised). */
static RectI dev_rect(const DCObj *dc, int16_t l, int16_t t, int16_t r, int16_t b) {
  RectI x = {l - dc->wox, t - dc->woy, r - dc->wox, b - dc->woy};
  if (x.l > x.r) { int32_t v = x.l; x.l = x.r; x.r = v; }
  if (x.t > x.b) { int32_t v = x.t; x.t = x.b; x.b = v; }
  return x;
}

int16_t ExcludeClipRect(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return GDI_ERROR_REGION;
  RL cur, res;
  rl_load(dc, &cur);
  if (!rl_subtract(&res, &cur, dev_rect(dc, l, t, r, b))) return GDI_ERROR_REGION;
  if (!rl_store(dc, &res)) return GDI_ERROR_REGION;
  return kind_in_surface(dc, &res);
}

int16_t IntersectClipRect(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return GDI_ERROR_REGION;
  RL cur, res;
  rl_load(dc, &cur);
  rl_intersect(&res, &cur, dev_rect(dc, l, t, r, b));
  if (!rl_store(dc, &res)) return GDI_ERROR_REGION;
  return kind_in_surface(dc, &res);
}

int16_t SelectClipRgn(HDC16 hdc, HRGN16 hrgn) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return GDI_ERROR_REGION;
  if (!hrgn) {
    RegionObj *old = clip_block(dc);
    if (old) rt_free(old);
    dc->clip_off = 0;
    return SIMPLEREGION;
  }
  RegionObj *rg = gdi_obj_get(hrgn, GDI_OBJ_REGION);
  if (!rg) return GDI_ERROR_REGION;
  RL l;
  l.n = rg->count;
  memcpy(l.r, rects_of(rg), (size_t)rg->count * sizeof(RectI));
  if (!rl_store(dc, &l)) return GDI_ERROR_REGION;
  return kind_of(l.n);
}

HRGN16 CreateRectRgn(int16_t l, int16_t t, int16_t r, int16_t b) {
  if (l > r) { int16_t v = l; l = r; r = v; }
  if (t > b) { int16_t v = t; t = b; b = v; }
  bool empty = (l == r || t == b);
  uint16_t h = gdi_obj_alloc(GDI_OBJ_REGION, sizeof(RegionObj) + sizeof(RectI));
  if (!h) return 0;
  RegionObj *rg = gdi_obj_get(h, GDI_OBJ_REGION);
  rg->count = empty ? 0 : 1;
  if (!empty) *rects_of(rg) = (RectI){l, t, r, b};
  return h;
}

void gdi_clip_free(DCObj *dc) {
  RegionObj *old = clip_block(dc);
  if (old) rt_free(old);
  dc->clip_off = 0;
}
