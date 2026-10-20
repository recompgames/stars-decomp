/* GDI core part 1 (task stars-0z4.4): state, object table, DCs, colours, palettes, lines and
 * shapes, device caps and misc. Blits and DIBs are in gdi_blit.c, regions in gdi_region.c, the
 * screen and system palette in framebuffer.c. Plain C: no harness dependency.
 *
 * Behaviour is specified from the Windows 3.1 SDK documentation with Wine's 16-bit code as a
 * behavioural reference; there are no reference captures yet (docs/PLAN.md decision 7), so
 * the pixel-exact shapes marked "unverified" in docs/inventory.md follow the definitions
 * given there. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/gdi_internal.h"
#include "runtime/text.h"

static uint32_t g_state_off; /* arena offset of the GdiState, 0 = not initialised */
static GdiSelectFontFn g_font_hook;

GdiState *gdi_state(void) { return g_state_off ? (GdiState *)(mem_arena() + g_state_off) : NULL; }

/* ---------------------------------------------------------------- object table */

static ObjSlot *slot_of(uint16_t h) {
  GdiState *g = gdi_state();
  if (!g || h < GDI_HANDLE_BASE || ((h - GDI_HANDLE_BASE) % GDI_HANDLE_STEP)) return NULL;
  unsigned i = (h - GDI_HANDLE_BASE) / GDI_HANDLE_STEP;
  if (i >= GDI_MAX_OBJS || !g->objs[i].off) return NULL;
  return &g->objs[i];
}

uint16_t gdi_obj_alloc(int type, uint32_t bytes) {
  GdiState *g = gdi_state();
  if (!g) return 0;
  for (unsigned i = 0; i < GDI_MAX_OBJS; i++) {
    if (g->objs[i].off) continue;
    void *p = rt_alloc(bytes ? bytes : 16);
    if (!p) return 0;
    memset(p, 0, bytes ? bytes : 16);
    g->objs[i].type = (uint8_t)type;
    g->objs[i].stock = 0;
    g->objs[i].refs = 0;
    g->objs[i].off = mem_off(p);
    return (uint16_t)(GDI_HANDLE_BASE + i * GDI_HANDLE_STEP);
  }
  return 0;
}

void *gdi_obj_get(uint16_t h, int type) {
  ObjSlot *s = slot_of(h);
  if (!s || s->type != type) return NULL;
  return mem_arena() + s->off;
}

bool gdi_obj_free(uint16_t h) {
  ObjSlot *s = slot_of(h);
  if (!s) return false;
  rt_free(mem_arena() + s->off);
  memset(s, 0, sizeof *s);
  return true;
}

void gdi_set_stock(int id, uint16_t handle) {
  GdiState *g = gdi_state();
  ObjSlot *s = slot_of(handle);
  if (!g || id < 0 || id > 16 || !s) return;
  g->stock[id] = handle;
  s->stock = 1;
}

void gdi_set_font_hook(GdiSelectFontFn fn) { g_font_hook = fn; }

static void ref(uint16_t h, int d) {
  ObjSlot *s = slot_of(h);
  if (s) s->refs = (uint16_t)(s->refs + d);
}
/* For the fonts task (runtime/text.c): selecting a font into a DC counts as a reference. */
void gdi_obj_ref(uint16_t h, int d) { ref(h, d); }

DCObj *gdi_dc_get(HDC16 h) { return gdi_obj_get(h, GDI_OBJ_DC); }

PaletteObj *gdi_palette_get(uint16_t h) { return gdi_obj_get(h, GDI_OBJ_PALETTE); }
uint8_t *gdi_palette_entries(PaletteObj *p) { return (uint8_t *)(p + 1); }
uint8_t *gdi_palette_map(PaletteObj *p) { return gdi_palette_entries(p) + 4u * p->n; }

/* ---------------------------------------------------------------- init / shutdown */

static uint16_t new_pen(int16_t style, int16_t width, uint32_t color) {
  uint16_t h = gdi_obj_alloc(GDI_OBJ_PEN, sizeof(PenObj));
  PenObj *p = gdi_obj_get(h, GDI_OBJ_PEN);
  if (!p) return 0;
  p->style = style;
  p->width = width;
  p->color = color;
  return h;
}

static uint16_t new_brush(int16_t style, uint32_t color) {
  uint16_t h = gdi_obj_alloc(GDI_OBJ_BRUSH, sizeof(BrushObj));
  BrushObj *b = gdi_obj_get(h, GDI_OBJ_BRUSH);
  if (!b) return 0;
  b->style = style;
  b->color = color;
  return h;
}

static uint16_t new_bitmap(int32_t w, int32_t h, bool mono) {
  if (w <= 0 || h <= 0 || (uint64_t)w * (uint64_t)h > GDI_MAX_BITMAP_PIXELS) return 0;
  uint16_t hb = gdi_obj_alloc(GDI_OBJ_BITMAP, sizeof(BitmapObj) + (uint32_t)(w * h));
  BitmapObj *b = gdi_obj_get(hb, GDI_OBJ_BITMAP);
  if (!b) return 0;
  b->w = w;
  b->h = h;
  b->mono = mono;
  return hb;
}

static uint16_t stock_obj(GdiState *g, uint16_t h, int id) {
  if (!h) return 0;
  gdi_set_stock(id, h);
  (void)g;
  return h;
}

int gdi_init(void) {
  if (g_state_off) return 0;
  GdiState *g = rt_alloc(sizeof *g);
  if (!g) return -1;
  memset(g, 0, sizeof *g);
  g_state_off = mem_off(g);
  fb_init_palette(g);
  if (fb_alloc_screen(g) != 0) {
    rt_free(g);
    g_state_off = 0;
    return -1;
  }
  /* Stock objects. */
  stock_obj(g, new_brush(BS_SOLID, GDI_RGB(255, 255, 255)), WHITE_BRUSH);
  stock_obj(g, new_brush(BS_SOLID, GDI_RGB(192, 192, 192)), LTGRAY_BRUSH);
  stock_obj(g, new_brush(BS_SOLID, GDI_RGB(128, 128, 128)), GRAY_BRUSH);
  stock_obj(g, new_brush(BS_SOLID, GDI_RGB(64, 64, 64)), DKGRAY_BRUSH);
  stock_obj(g, new_brush(BS_SOLID, GDI_RGB(0, 0, 0)), BLACK_BRUSH);
  stock_obj(g, new_brush(BS_HOLLOW, 0), NULL_BRUSH);
  stock_obj(g, new_pen(PS_SOLID, 1, GDI_RGB(255, 255, 255)), WHITE_PEN);
  stock_obj(g, new_pen(PS_SOLID, 1, GDI_RGB(0, 0, 0)), BLACK_PEN);
  stock_obj(g, new_pen(PS_NULL, 1, 0), NULL_PEN);
  /* Default palette: the 20 static colours; logical entry i maps to system 0-9, 246-255. */
  uint16_t hp = gdi_obj_alloc(GDI_OBJ_PALETTE, sizeof(PaletteObj) + 20 * 4 + 20);
  PaletteObj *pal = gdi_palette_get(hp);
  if (pal) {
    pal->n = 20;
    pal->realized = 1;
    for (unsigned i = 0; i < 20; i++) {
      unsigned si = i < 10 ? i : 246 + (i - 10);
      uint8_t *e = gdi_palette_entries(pal) + 4 * i;
      e[0] = g->sys_rgb[si][0];
      e[1] = g->sys_rgb[si][1];
      e[2] = g->sys_rgb[si][2];
      e[3] = 0;
      gdi_palette_map(pal)[i] = (uint8_t)si;
    }
    g->default_palette = hp;
    stock_obj(g, hp, DEFAULT_PALETTE);
  }
  uint16_t hb = new_bitmap(1, 1, true);
  if (hb) {
    g->default_bitmap = hb;
    slot_of(hb)->stock = 1;
  }
  if (g->stock[BLACK_PEN] && g->stock[WHITE_BRUSH] && g->default_bitmap && g->default_palette)
    return text_init(); /* stock fonts and the SelectObject font hook (runtime/text.c) */
  return -1;
}

void gdi_shutdown(void) {
  GdiState *g = gdi_state();
  if (!g) return;
  for (unsigned i = 0; i < GDI_MAX_OBJS; i++) {
    if (!g->objs[i].off) continue;
    if (g->objs[i].type == GDI_OBJ_DC) gdi_clip_free((DCObj *)(mem_arena() + g->objs[i].off));
    rt_free(mem_arena() + g->objs[i].off);
  }
  rt_free(mem_arena() + g->screen_off);
  rt_free(g);
  g_state_off = 0;
  g_font_hook = NULL;
}

/* ---------------------------------------------------------------- DCs */

static HDC16 dc_new(bool memory) {
  GdiState *g = gdi_state();
  if (!g) return 0;
  uint16_t h = gdi_obj_alloc(GDI_OBJ_DC, sizeof(DCObj));
  DCObj *dc = gdi_obj_get(h, GDI_OBJ_DC);
  if (!dc) return 0;
  dc->memory = memory;
  dc->bkmode = OPAQUE;
  dc->rop2 = R2_COPYPEN;
  dc->polyfill = 1;
  dc->stretch = 1;
  dc->hpen = g->stock[BLACK_PEN];
  dc->hbrush = g->stock[WHITE_BRUSH];
  dc->hpal = g->default_palette;
  dc->bkcolor = GDI_RGB(255, 255, 255);
  dc->textcolor = 0;
  dc->hard = (RectI){0, 0, g->screen_w, g->screen_h};
  ref(dc->hpen, 1);
  ref(dc->hbrush, 1);
  ref(dc->hpal, 1);
  dc->hfont = g->stock[SYSTEM_FONT];
  ref(dc->hfont, 1);
  if (memory) {
    dc->hbitmap = g->default_bitmap;
    ref(dc->hbitmap, 1);
  }
  return h;
}

HDC16 gdi_create_screen_dc(void) { return dc_new(false); }
HDC16 CreateCompatibleDC(HDC16 hdc) {
  (void)hdc; /* the only display is the 8-bpp screen */
  return dc_new(true);
}

uint16_t DeleteDC(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  ref(dc->hpen, -1);
  ref(dc->hbrush, -1);
  ref(dc->hpal, -1);
  ref(dc->hfont, -1);
  if (dc->memory) ref(dc->hbitmap, -1);
  gdi_clip_free(dc);
  gdi_obj_free(hdc);
  return 1;
}

void gdi_dc_set_origin(HDC16 hdc, int16_t x, int16_t y) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return;
  dc->vox = x;
  dc->voy = y;
}

void gdi_dc_set_hard_clip(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b) {
  DCObj *dc = gdi_dc_get(hdc);
  if (dc) dc->hard = (RectI){l, t, r, b};
}

bool dc_surface(const DCObj *dc, Surf *s) {
  GdiState *g = gdi_state();
  if (dc->memory) {
    BitmapObj *b = gdi_obj_get(dc->hbitmap, GDI_OBJ_BITMAP);
    if (!b) return false;
    s->p = (uint8_t *)(b + 1);
    s->w = b->w;
    s->h = b->h;
    s->mono = b->mono;
  } else {
    s->p = mem_arena() + g->screen_off;
    s->w = g->screen_w;
    s->h = g->screen_h;
    s->mono = 0;
  }
  return true;
}

static bool dc_is_mono(const DCObj *dc) {
  Surf s;
  return dc_surface(dc, &s) && s.mono;
}

/* ---------------------------------------------------------------- state functions */

uint32_t SetBkColor(HDC16 hdc, COLORREF16 c) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  uint32_t old = dc->bkcolor;
  dc->bkcolor = c;
  return old;
}
uint32_t GetBkColor(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  return dc ? dc->bkcolor : 0;
}
uint32_t SetTextColor(HDC16 hdc, COLORREF16 c) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  uint32_t old = dc->textcolor;
  dc->textcolor = c;
  return old;
}
COLORREF16 GetTextColor(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  return dc ? dc->textcolor : 0;
}
int16_t SetBkMode(HDC16 hdc, int16_t mode) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  int16_t old = dc->bkmode;
  dc->bkmode = mode;
  return old;
}
int16_t GetBkMode(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  return dc ? dc->bkmode : 0;
}
int16_t SetROP2(HDC16 hdc, int16_t mode) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc || mode < R2_BLACK || mode > R2_WHITE) return 0;
  int16_t old = dc->rop2;
  dc->rop2 = mode;
  return old;
}
int16_t GetROP2(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  return dc ? dc->rop2 : 0;
}

static uint32_t pack_pt(int32_t x, int32_t y) { return (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16); }

uint32_t SetWindowOrg(HDC16 hdc, int16_t x, int16_t y) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  uint32_t old = pack_pt(dc->wox, dc->woy);
  dc->wox = x;
  dc->woy = y;
  return old;
}
uint32_t SetBrushOrg(HDC16 hdc, int16_t x, int16_t y) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  uint32_t old = pack_pt(dc->box, dc->boy);
  dc->box = x;
  dc->boy = y;
  return old;
}

uint32_t MoveTo(HDC16 hdc, int16_t x, int16_t y) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  uint32_t old = pack_pt(dc->curx, dc->cury);
  dc->curx = x;
  dc->cury = y;
  return old;
}

/* ---------------------------------------------------------------- colours */

static void sys_rgb_of(int idx, int rgb[3]) {
  GdiState *g = gdi_state();
  rgb[0] = g->sys_rgb[idx][0];
  rgb[1] = g->sys_rgb[idx][1];
  rgb[2] = g->sys_rgb[idx][2];
}

/* Lazily maps a never-realized palette (background use) onto existing system entries. */
static void palette_ensure_map(PaletteObj *p) {
  if (p->realized) return;
  uint8_t *e = gdi_palette_entries(p), *m = gdi_palette_map(p);
  for (unsigned i = 0; i < p->n; i++) m[i] = gdi_sys_nearest(e[4 * i], e[4 * i + 1], e[4 * i + 2]);
}

static uint8_t pal_nearest_logical(PaletteObj *p, int r, int g, int b) {
  uint8_t *e = gdi_palette_entries(p);
  long best = LONG_MAX;
  unsigned bi = 0;
  bool found = false;
  for (unsigned i = 0; i < p->n; i++) {
    if (e[4 * i + 3] & PC_RESERVED) continue; /* reserved entries are never matched */
    long dr = e[4 * i] - r, dg = e[4 * i + 1] - g, db = e[4 * i + 2] - b;
    long d = dr * dr + dg * dg + db * db;
    if (d < best) { best = d; bi = i; found = true; }
  }
  if (!found) return gdi_static_nearest(r, g, b);
  return gdi_palette_map(p)[bi];
}

/* System index that a COLORREF resolves to for pens, text and SetPixel. Plain RGB (kind 0)
 * goes to the nearest of the 20 STATIC colours; only PALETTEINDEX / PALETTERGB use the
 * realized entries. */
static uint8_t sys_index_for(const DCObj *dc, COLORREF16 c) {
  unsigned kind = c >> 24;
  PaletteObj *p = gdi_palette_get(dc->hpal);
  if (kind == 1 && p) {
    palette_ensure_map(p);
    unsigned i = c & 0xFFFF;
    return i < p->n ? gdi_palette_map(p)[i] : 0;
  }
  int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
  if (kind == 2 && p && p->n) {
    palette_ensure_map(p);
    return pal_nearest_logical(p, r, g, b);
  }
  return gdi_static_nearest(r, g, b);
}

/* A DIB colour (colour table entry or 24-bpp pixel) maps through the whole system palette,
 * realized entries included. */
uint8_t gdi_dib_index(const DCObj *dc, int r, int g, int b) {
  uint8_t i = gdi_sys_nearest(r, g, b);
  if (dc_is_mono(dc)) {
    int rgb[3];
    sys_rgb_of(i, rgb);
    return gdi_mono_index(rgb[0], rgb[1], rgb[2]);
  }
  return i;
}

void gdi_colorref_rgb(const DCObj *dc, COLORREF16 c, int rgb[3]) {
  if ((c >> 24) == 1 || (c >> 24) == 2) {
    sys_rgb_of(sys_index_for(dc, c), rgb);
    return;
  }
  rgb[0] = c & 255;
  rgb[1] = (c >> 8) & 255;
  rgb[2] = (c >> 16) & 255;
}

uint8_t gdi_pen_index(const DCObj *dc, COLORREF16 c) {
  uint8_t i = sys_index_for(dc, c);
  if (dc_is_mono(dc)) {
    int rgb[3];
    sys_rgb_of(i, rgb);
    return gdi_mono_index(rgb[0], rgb[1], rgb[2]);
  }
  return i;
}

uint8_t gdi_dc_color_index(HDC16 hdc, COLORREF16 c) {
  DCObj *dc = gdi_dc_get(hdc);
  return dc ? gdi_pen_index(dc, c) : 0;
}

static const uint8_t k_bayer[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26}, {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22}, {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21}};

/* Dither a colour that is not in the system palette from the 20 static colours: choose the
 * pair (a, b) and the fraction k/64 whose mix is nearest, then an 8x8 ordered (Bayer)
 * pattern. Unverified against the real driver (no reference captures yet). */
static void dither(int r, int g, int b, uint8_t out[64]) {
  GdiState *gs = gdi_state();
  static const int sidx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
  long best = LONG_MAX;
  int ba = 0, bb = 0, bk = 0;
  for (int a = 0; a < 20; a++)
    for (int c = a; c < 20; c++)
      for (int k = 0; k <= (a == c ? 0 : 64); k++) {
        long e = 0;
        for (int ch = 0; ch < 3; ch++) {
          long mixed = (long)gs->sys_rgb[sidx[a]][ch] * (64 - k) + (long)gs->sys_rgb[sidx[c]][ch] * k;
          long want = (long)(ch == 0 ? r : ch == 1 ? g : b) * 64;
          e += (mixed - want) * (mixed - want);
        }
        if (e < best) { best = e; ba = a; bb = c; bk = k; }
      }
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) out[y * 8 + x] = (uint8_t)sidx[k_bayer[y][x] < bk ? bb : ba];
}

void gdi_brush_pattern(const DCObj *dc, BrushObj *b, uint8_t out[64]) {
  GdiState *g = gdi_state();
  bool mono = dc_is_mono(dc);
  if (b->style == BS_PATTERN) {
    if (b->mono) {
      uint8_t t = gdi_pen_index(dc, dc->textcolor), k = gdi_pen_index(dc, dc->bkcolor);
      for (int i = 0; i < 64; i++) out[i] = b->pat[i] ? k : t;
    } else if (mono) {
      for (int i = 0; i < 64; i++) {
        int rgb[3];
        sys_rgb_of(b->pat[i], rgb);
        out[i] = gdi_mono_index(rgb[0], rgb[1], rgb[2]);
      }
    } else {
      memcpy(out, b->pat, 64);
    }
    return;
  }
  /* Solid: palette-relative colours and exact system colours are solid; others are dithered. */
  if (!mono && (b->color >> 24) == 0) {
    if (b->cache_valid && b->cache_gen == g->pal_gen && b->cache_pal == dc->hpal) {
      memcpy(out, b->cache, 64);
      return;
    }
    int r = b->color & 255, gr = (b->color >> 8) & 255, bl = (b->color >> 16) & 255;
    int ex = gdi_static_exact(r, gr, bl);
    if (ex >= 0) memset(b->cache, ex, 64);
    else dither(r, gr, bl, b->cache);
    b->cache_valid = 1;
    b->cache_gen = g->pal_gen;
    b->cache_pal = dc->hpal;
    memcpy(out, b->cache, 64);
    return;
  }
  memset(out, gdi_pen_index(dc, b->color), 64);
}

/* ---------------------------------------------------------------- pen / brush objects */

HPEN16 CreatePen(int16_t style, int16_t width, COLORREF16 c) {
  if (style < PS_SOLID || style > PS_INSIDEFRAME) return 0;
  return new_pen(style, width < 0 ? -width : width, c);
}

HBRUSH16 CreateSolidBrush(COLORREF16 c) { return new_brush(BS_SOLID, c); }

HBRUSH16 CreatePatternBrush(HBITMAP16 hbm) {
  BitmapObj *bm = gdi_obj_get(hbm, GDI_OBJ_BITMAP);
  if (!bm) return 0;
  uint16_t h = new_brush(BS_PATTERN, 0);
  BrushObj *b = gdi_obj_get(h, GDI_OBJ_BRUSH);
  if (!b) return 0;
  const uint8_t *px = (const uint8_t *)(bm + 1);
  b->mono = bm->mono;
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) b->pat[y * 8 + x] = px[(y % bm->h) * bm->w + (x % bm->w)];
  return h;
}

/* ---------------------------------------------------------------- bitmaps */

HBITMAP16 CreateCompatibleBitmap(HDC16 hdc, int16_t w, int16_t h) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  if (w <= 0 || h <= 0) return new_bitmap(1, 1, true);
  return new_bitmap(w, h, dc_is_mono(dc));
}

uint8_t *gdi_bitmap_bits(HBITMAP16 h, int *w, int *hgt, bool *mono) {
  BitmapObj *b = gdi_obj_get(h, GDI_OBJ_BITMAP);
  if (!b) return NULL;
  if (w) *w = b->w;
  if (hgt) *hgt = b->h;
  if (mono) *mono = b->mono != 0;
  return (uint8_t *)(b + 1);
}

/* ---------------------------------------------------------------- select / delete / get */

HGDIOBJ16 SelectObject(HDC16 hdc, HGDIOBJ16 h) {
  DCObj *dc = gdi_dc_get(hdc);
  ObjSlot *s = slot_of(h);
  if (!dc || !s) return 0;
  uint16_t old;
  switch (s->type) {
    case GDI_OBJ_PEN:
      old = dc->hpen;
      ref(old, -1);
      ref(h, 1);
      dc->hpen = h;
      return old;
    case GDI_OBJ_BRUSH:
      old = dc->hbrush;
      ref(old, -1);
      ref(h, 1);
      dc->hbrush = h;
      return old;
    case GDI_OBJ_BITMAP:
      if (!dc->memory) return 0;
      if (h == dc->hbitmap) return h;
      if (s->refs && h != gdi_state()->default_bitmap) return 0; /* already in another DC */
      old = dc->hbitmap;
      ref(old, -1);
      ref(h, 1);
      dc->hbitmap = h;
      return old;
    case GDI_OBJ_REGION:
      return (HGDIOBJ16)SelectClipRgn(hdc, h);
    case GDI_OBJ_FONT:
      return g_font_hook ? g_font_hook(hdc, h) : 0;
    default:
      return 0;
  }
}

uint16_t DeleteObject(HGDIOBJ16 h) {
  ObjSlot *s = slot_of(h);
  if (!s || s->stock || s->refs || s->type == GDI_OBJ_DC) return 0;
  return gdi_obj_free(h) ? 1 : 0;
}

HGDIOBJ16 GetStockObject(int16_t id) {
  GdiState *g = gdi_state();
  if (!g || id < 0 || id > 16) return 0;
  return g->stock[id];
}

uint16_t UnrealizeObject(HGDIOBJ16 h) {
  ObjSlot *s = slot_of(h);
  if (!s) return 0;
  if (s->type == GDI_OBJ_BRUSH) ((BrushObj *)(mem_arena() + s->off))->cache_valid = 0;
  else if (s->type == GDI_OBJ_PALETTE) ((PaletteObj *)(mem_arena() + s->off))->realized = 0;
  return 1;
}

static void wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v); wr16(p + 2, v >> 16); }

int16_t GetObject(HGDIOBJ16 h, int16_t cb, FARPTR lp) {
  ObjSlot *s = slot_of(h);
  if (!s || cb <= 0 || !lp) return 0;
  uint8_t tmp[16];
  int n = 0;
  void *obj = mem_arena() + s->off;
  memset(tmp, 0, sizeof tmp);
  switch (s->type) {
    case GDI_OBJ_BITMAP: {
      BitmapObj *b = obj;
      int bpp = b->mono ? 1 : 8;
      wr16(tmp + 0, 0);
      wr16(tmp + 2, (uint32_t)b->w);
      wr16(tmp + 4, (uint32_t)b->h);
      wr16(tmp + 6, (uint32_t)(((b->w * bpp + 15) / 16) * 2));
      tmp[8] = 1;
      tmp[9] = (uint8_t)bpp;
      n = 14; /* bmBits (far pointer) stays 0 */
      break;
    }
    case GDI_OBJ_PEN: {
      PenObj *p = obj;
      wr16(tmp + 0, (uint32_t)p->style);
      wr16(tmp + 2, (uint32_t)p->width);
      wr16(tmp + 4, 0);
      wr32(tmp + 6, p->color);
      n = 10;
      break;
    }
    case GDI_OBJ_BRUSH: {
      BrushObj *b = obj;
      wr16(tmp + 0, (uint32_t)b->style);
      wr32(tmp + 2, b->color);
      wr16(tmp + 6, 0);
      n = 8;
      break;
    }
    case GDI_OBJ_PALETTE:
      wr16(tmp, ((PaletteObj *)obj)->n);
      n = 2;
      break;
    default:
      return 0;
  }
  if (n > cb) n = cb;
  if (!mem_check(lp, (uint32_t)n)) return 0;
  memcpy(LIN(lp), tmp, (size_t)n);
  return (int16_t)n;
}

/* ---------------------------------------------------------------- palettes */

HPALETTE16 CreatePalette(FARPTR lp) {
  if (!lp || !mem_check(lp, 4)) return 0;
  const uint8_t *q = LIN(lp);
  unsigned n = q[2] | ((unsigned)q[3] << 8);
  if (n == 0 || n > 1024 || !mem_check(lp, 4 + 4 * n)) return 0;
  uint16_t h = gdi_obj_alloc(GDI_OBJ_PALETTE, (uint32_t)(sizeof(PaletteObj) + 5 * n));
  PaletteObj *p = gdi_palette_get(h);
  if (!p) return 0;
  p->n = (uint16_t)n;
  memcpy(gdi_palette_entries(p), q + 4, 4 * n);
  return h;
}

HPALETTE16 SelectPalette(HDC16 hdc, HPALETTE16 hpal, uint16_t background) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc || !gdi_palette_get(hpal)) return 0;
  uint16_t old = dc->hpal;
  ref(old, -1);
  ref(hpal, 1);
  dc->hpal = hpal;
  dc->pal_bg = background ? 1 : 0;
  return old;
}

uint16_t RealizePalette(HDC16 hdc) {
  DCObj *dc = gdi_dc_get(hdc);
  GdiState *g = gdi_state();
  PaletteObj *p = dc ? gdi_palette_get(dc->hpal) : NULL;
  if (!p) return 0;
  uint8_t *e = gdi_palette_entries(p), *m = gdi_palette_map(p);
  uint8_t oldmap[1024];
  bool had = p->realized != 0;
  memcpy(oldmap, m, p->n);
  if (!dc->pal_bg && dc->hpal != g->default_palette) {
    /* Foreground: the logical palette takes over the 236 free system entries. */
    uint8_t save_rgb[256][3], save_used[256], save_res[256];
    memcpy(save_rgb, g->sys_rgb, sizeof save_rgb);
    memcpy(save_used, g->sys_used, sizeof save_used);
    memcpy(save_res, g->sys_reserved, sizeof save_res);
    for (int i = 10; i < 246; i++) { g->sys_used[i] = 0; g->sys_reserved[i] = 0; memset(g->sys_rgb[i], 0, 3); }
    for (unsigned i = 0; i < p->n; i++) {
      uint8_t *pe = e + 4 * i;
      if (pe[3] & PC_EXPLICIT) { m[i] = pe[0]; continue; }
      /* PC_RESERVED (needs a reference capture, see docs/inventory.md section 11): never
       * collapses onto an existing colour, takes its own unused slot, and is never matched
       * by other colours. */
      bool resv = (pe[3] & PC_RESERVED) != 0;
      int ex = (resv || (pe[3] & PC_NOCOLLAPSE)) ? -1 : gdi_sys_exact(pe[0], pe[1], pe[2]);
      if (ex >= 0) { m[i] = (uint8_t)ex; continue; }
      int slot = -1;
      for (int k = 10; k < 246; k++)
        if (!g->sys_used[k]) { slot = k; break; }
      if (slot < 0) { m[i] = gdi_sys_nearest(pe[0], pe[1], pe[2]); continue; }
      g->sys_used[slot] = 1;
      g->sys_reserved[slot] = resv;
      memcpy(g->sys_rgb[slot], pe, 3);
      m[i] = (uint8_t)slot;
    }
    if (memcmp(save_rgb, g->sys_rgb, sizeof save_rgb) || memcmp(save_used, g->sys_used, sizeof save_used) ||
        memcmp(save_res, g->sys_reserved, sizeof save_res))
      g->pal_gen++;
  } else {
    /* Background (or the default palette): map to what the system palette already holds. */
    for (unsigned i = 0; i < p->n; i++) m[i] = gdi_sys_nearest(e[4 * i], e[4 * i + 1], e[4 * i + 2]);
  }
  p->realized = 1;
  uint16_t changed = 0;
  for (unsigned i = 0; i < p->n; i++)
    if (!had || oldmap[i] != m[i]) changed++;
  return changed;
}

/* ---------------------------------------------------------------- pixels and lines */

uint32_t SetPixel(HDC16 hdc, int16_t x, int16_t y, COLORREF16 c) {
  DCObj *dc = gdi_dc_get(hdc);
  Surf s;
  if (!dc || !dc_surface(dc, &s)) return 0xFFFFFFFFu;
  int32_t sx = gdi_sx(dc, x), sy = gdi_sy(dc, y);
  if (!gdi_visible(dc, &s, sx, sy)) return 0xFFFFFFFFu;
  uint8_t idx = gdi_pen_index(dc, c);
  s.p[sy * s.w + sx] = idx;
  if (s.mono) return idx ? GDI_RGB(255, 255, 255) : GDI_RGB(0, 0, 0);
  int rgb[3];
  sys_rgb_of(idx, rgb);
  return GDI_RGB(rgb[0], rgb[1], rgb[2]);
}

bool gdi_dc_put_pixel(HDC16 hdc, int16_t x, int16_t y, uint8_t index) {
  DCObj *dc = gdi_dc_get(hdc);
  Surf s;
  if (!dc || !dc_surface(dc, &s)) return false;
  int32_t sx = gdi_sx(dc, x), sy = gdi_sy(dc, y);
  if (!gdi_visible(dc, &s, sx, sy)) return false;
  s.p[sy * s.w + sx] = index;
  return true;
}

void gdi_put_rop2(const DCObj *dc, Surf *s, int32_t sx, int32_t sy, uint8_t pen_index) {
  uint8_t *d = &s->p[sy * s->w + sx];
  *d = (uint8_t)(gdi_apply_rop2(dc->rop2, pen_index, *d) & (s->mono ? 1 : 0xFF));
}

static void plot_pen(const DCObj *dc, Surf *s, int32_t sx, int32_t sy, uint8_t pidx) {
  if (gdi_visible(dc, s, sx, sy)) gdi_put_rop2(dc, s, sx, sy, pidx);
}

/* On/off masks for the dashed styles, one bit per pixel along the line, starting at the
 * first pixel of each LineTo. Unverified against the real driver. */
static bool dash_on(int style, int i) {
  switch (style) {
    case PS_DASH: return ((0x003Fu >> (i % 8)) & 1) != 0;
    case PS_DOT: return (i & 1) == 0;
    case PS_DASHDOT: return ((0x033Fu >> (i % 12)) & 1) != 0;
    case PS_DASHDOTDOT: return ((0x333Fu >> (i % 16)) & 1) != 0;
    default: return true;
  }
}

uint16_t LineTo(HDC16 hdc, int16_t x, int16_t y) {
  DCObj *dc = gdi_dc_get(hdc);
  Surf s;
  if (!dc || !dc_surface(dc, &s)) return 0;
  PenObj *pen = gdi_obj_get(dc->hpen, GDI_OBJ_PEN);
  int32_t x0 = dc->curx, y0 = dc->cury;
  dc->curx = x;
  dc->cury = y;
  if (!pen || pen->style == PS_NULL) return 1;
  uint8_t pidx = gdi_pen_index(dc, pen->color);
  uint8_t bidx = gdi_pen_index(dc, dc->bkcolor);
  int32_t dx = abs(x - x0), dy = abs(y - y0), stx = x0 < x ? 1 : -1, sty = y0 < y ? 1 : -1;
  int32_t n = dx > dy ? dx : dy; /* the end point is not drawn */
  int32_t err = dx - dy, cx = x0, cy = y0;
  int w = pen->width < 1 ? 1 : pen->width;

  if (w == 1) {
    for (int32_t i = 0; i < n; i++) {
      bool on = dash_on(pen->style, i);
      if (on) plot_pen(dc, &s, gdi_sx(dc, cx), gdi_sy(dc, cy), pidx);
      else if (dc->bkmode == OPAQUE) plot_pen(dc, &s, gdi_sx(dc, cx), gdi_sy(dc, cy), bidx);
      int32_t e2 = 2 * err;
      if (e2 > -dy) { err -= dy; cx += stx; }
      if (e2 < dx) { err += dx; cy += sty; }
    }
    return 1;
  }

  /* Wide pen (always solid in Windows 3.1): stamp w x w squares into a coverage mask, then
   * apply the ROP once per covered pixel, so XOR modes do not double-hit overlaps. */
  int32_t lo = (w - 1) / 2, hi = w - lo; /* stamp covers [c - lo, c - lo + w) */
  int32_t minx = (x0 < x ? x0 : x) - lo, maxx = (x0 > x ? x0 : x) + hi;
  int32_t miny = (y0 < y ? y0 : y) - lo, maxy = (y0 > y ? y0 : y) + hi;
  int32_t bx0 = minx, by0 = miny, bw = maxx - minx + 1, bh = maxy - miny + 1;
  if (bw <= 0 || bh <= 0 || (int64_t)bw * bh > 4 * 1024 * 1024) return 1;
  uint8_t *mask = rt_alloc((size_t)bw * (size_t)bh);
  if (!mask) return 0;
  memset(mask, 0, (size_t)bw * (size_t)bh);
  int32_t px = x0, py = y0;
  for (int32_t i = 0; i < n; i++) {
    for (int32_t yy = py - lo; yy < py - lo + w; yy++)
      for (int32_t xx = px - lo; xx < px - lo + w; xx++) mask[(yy - by0) * bw + (xx - bx0)] = 1;
    int32_t e2 = 2 * err;
    if (e2 > -dy) { err -= dy; px += stx; }
    if (e2 < dx) { err += dx; py += sty; }
  }
  for (int32_t yy = 0; yy < bh; yy++)
    for (int32_t xx = 0; xx < bw; xx++)
      if (mask[yy * bw + xx]) plot_pen(dc, &s, gdi_sx(dc, bx0 + xx), gdi_sy(dc, by0 + yy), pidx);
  rt_free(mask);
  return 1;
}

/* ---------------------------------------------------------------- Rectangle / Ellipse */

typedef struct Fill {
  const DCObj *dc;
  Surf *s;
  bool have_brush;
  uint8_t pat[64];
} Fill;

static void fill_prepare(Fill *f, const DCObj *dc, Surf *s) {
  f->dc = dc;
  f->s = s;
  BrushObj *b = gdi_obj_get(dc->hbrush, GDI_OBJ_BRUSH);
  f->have_brush = b && b->style != BS_HOLLOW;
  if (f->have_brush) gdi_brush_pattern(dc, b, f->pat);
}

static void fill_px(const Fill *f, int32_t sx, int32_t sy) {
  if (!f->have_brush || !gdi_visible(f->dc, f->s, sx, sy)) return;
  int px = (int)((sx - f->dc->vox - f->dc->box) & 7), py = (int)((sy - f->dc->voy - f->dc->boy) & 7);
  gdi_put_rop2(f->dc, f->s, sx, sy, f->pat[py * 8 + px]);
}

static int pen_width(const PenObj *pen) {
  if (!pen || pen->style == PS_NULL) return 0;
  return pen->width < 1 ? 1 : pen->width;
}

uint16_t Rectangle(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b) {
  DCObj *dc = gdi_dc_get(hdc);
  Surf s;
  if (!dc || !dc_surface(dc, &s)) return 0;
  if (l > r) { int16_t v = l; l = r; r = v; }
  if (t > b) { int16_t v = t; t = b; b = v; }
  PenObj *pen = gdi_obj_get(dc->hpen, GDI_OBJ_PEN);
  int pw = pen_width(pen);
  uint8_t pidx = pw ? gdi_pen_index(dc, pen->color) : 0;
  Fill f;
  fill_prepare(&f, dc, &s);
  int32_t sl = gdi_sx(dc, l), sr = gdi_sx(dc, r), st = gdi_sy(dc, t), sb = gdi_sy(dc, b);
  /* Clamp the scan to the surface; outside pixels are invisible anyway. */
  int32_t y0 = st < 0 ? 0 : st, y1 = sb > s.h ? s.h : sb, x0 = sl < 0 ? 0 : sl, x1 = sr > s.w ? s.w : sr;
  for (int32_t y = y0; y < y1; y++)
    for (int32_t x = x0; x < x1; x++) {
      bool border = x < sl + pw || x >= sr - pw || y < st + pw || y >= sb - pw;
      if (border) {
        if (pw) plot_pen(dc, &s, x, y, pidx);
        /* with a NULL pen pw is 0 and there is no border pixel */
      } else {
        fill_px(&f, x, y);
      }
    }
  return 1;
}

/* A pixel is inside the ellipse of the bounding box [l,r) x [t,b) when its centre is:
 * (dx*B)^2 + (dy*A)^2 <= (A*B)^2 with dx, dy the doubled offsets from the box centre. */
typedef struct Ell {
  int32_t l, r, t, b;
  int64_t A, B, cx, cy;
} Ell;

static bool ell_in(const Ell *e, int32_t x, int32_t y) {
  if (x < e->l || x >= e->r || y < e->t || y >= e->b) return false;
  int64_t dx = 2 * (int64_t)x + 1 - e->cx, dy = 2 * (int64_t)y + 1 - e->cy;
  return (dx * e->B) * (dx * e->B) + (dy * e->A) * (dy * e->A) <= (e->A * e->B) * (e->A * e->B);
}

uint16_t Ellipse(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b) {
  DCObj *dc = gdi_dc_get(hdc);
  Surf s;
  if (!dc || !dc_surface(dc, &s)) return 0;
  if (l > r) { int16_t v = l; l = r; r = v; }
  if (t > b) { int16_t v = t; t = b; b = v; }
  if (l == r || t == b) return 1;
  PenObj *pen = gdi_obj_get(dc->hpen, GDI_OBJ_PEN);
  int pw = pen_width(pen);
  uint8_t pidx = pw ? gdi_pen_index(dc, pen->color) : 0;
  Fill f;
  fill_prepare(&f, dc, &s);
  Ell e = {l, r, t, b, (int64_t)r - l, (int64_t)b - t, (int64_t)l + r, (int64_t)t + b};
  for (int32_t y = t; y < b; y++)
    for (int32_t x = l; x < r; x++) {
      int32_t sx = gdi_sx(dc, x), sy = gdi_sy(dc, y);
      if (sx < 0 || sy < 0 || sx >= s.w || sy >= s.h) continue;
      if (!ell_in(&e, x, y)) continue;
      bool border = false;
      for (int k = 1; k <= pw && !border; k++)
        border = !ell_in(&e, x - k, y) || !ell_in(&e, x + k, y) || !ell_in(&e, x, y - k) || !ell_in(&e, x, y + k);
      if (border) plot_pen(dc, &s, sx, sy, pidx);
      else fill_px(&f, sx, sy);
    }
  return 1;
}

/* ---------------------------------------------------------------- caps, MulDiv, Escape */

int16_t GetDeviceCaps(HDC16 hdc, int16_t index) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return 0;
  Surf s;
  bool mono = dc_surface(dc, &s) && s.mono;
  switch (index) {
    case DRIVERVERSION: return 0x0300;
    case TECHNOLOGY: return 1; /* DT_RASDISPLAY */
    case HORZSIZE: return 211; /* 800 px at 96 dpi, in mm */
    case VERTSIZE: return 159;
    case HORZRES: return GDI_SCREEN_W;
    case VERTRES: return GDI_SCREEN_H;
    case BITSPIXEL: return mono ? 1 : 8;
    case PLANES: return 1;
    case NUMBRUSHES: return -1;
    case NUMPENS: return 100;
    case NUMMARKERS: return 0;
    case NUMFONTS: return 0;
    case NUMCOLORS: return GDI_NUM_RESERVED;
    case PDEVICESIZE: return 0;
    case CURVECAPS: return 0x00FF;
    case LINECAPS: return 0x00FF;
    case POLYGONALCAPS: return 0x00FF;
    case TEXTCAPS: return 0x7F37;
    case CLIPCAPS: return 1;
    /* RC_BITBLT 1 | RC_BITMAP64 8 | RC_DI_BITMAP 0x80 | RC_PALETTE 0x100 | RC_DIBTODEV 0x200 |
     * RC_STRETCHBLT 0x800 | RC_STRETCHDIB 0x2000 */
    case RASTERCAPS: return 0x2B89;
    case ASPECTX: return 36;
    case ASPECTY: return 36;
    case ASPECTXY: return 51;
    case LOGPIXELSX: return 96;
    case LOGPIXELSY: return 96;
    case SIZEPALETTE: return GDI_SIZE_PALETTE;
    case NUMRESERVED: return GDI_NUM_RESERVED;
    case COLORRES: return 18;
    default: return 0;
  }
}

/* Wine MulDiv16: wine-mirror/wine 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
 * dlls/gdi.exe16/gdi.c lines 1920-1939. */
int16_t MulDiv(int16_t a, int16_t b, int16_t c) {
  int ret;
  if (!c) return -32768;
  if (c < 0) {
    a = (int16_t)-a;
    c = (int16_t)-c;
  }
  if ((a < 0 && b < 0) || (a >= 0 && b >= 0)) ret = (((int)a * b) + (c / 2)) / c;
  else ret = (((int)a * b) - (c / 2)) / c;
  if (ret > 32767 || ret < -32767) return -32768;
  return (int16_t)ret;
}

/* Printing escapes: every code returns 0 (not supported) until the printing task. */
int16_t Escape(HDC16 hdc, int16_t code, int16_t cbIn, FARPTR in, FARPTR out) {
  (void)hdc; (void)code; (void)cbIn; (void)in; (void)out;
  return 0;
}
