/* Internal structures shared by runtime/gdi*.c and runtime/framebuffer.c. Not for other
 * modules: use runtime/gdi.h. Everything here lives in the arena (rt_alloc), referenced by
 * arena offsets and 16-bit handles, never by host pointers stored in the arena. */
#ifndef STARS_RUNTIME_GDI_INTERNAL_H
#define STARS_RUNTIME_GDI_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "runtime/gdi.h"
#include "runtime/mem.h"

#define GDI_MAX_OBJS 2048
#define GDI_HANDLE_BASE 0x1000u
#define GDI_HANDLE_STEP 4u
#define GDI_MAX_CLIP_RECTS 256
#define GDI_BIG 0x40000000 /* "no clip" extent in clip arithmetic */
#define GDI_MAX_BITMAP_PIXELS (4u * 1024 * 1024)

typedef struct RectI {
  int32_t l, t, r, b; /* right and bottom exclusive */
} RectI;

typedef struct ObjSlot {
  uint8_t type;  /* GDI_OBJ_* */
  uint8_t stock; /* cannot be deleted */
  uint16_t refs; /* selected into this many DCs */
  uint32_t off;  /* arena offset of the payload, 0 = free slot */
} ObjSlot;

typedef struct GdiState {
  uint32_t magic;
  ObjSlot objs[GDI_MAX_OBJS];
  uint8_t sys_rgb[256][3]; /* system palette */
  uint8_t sys_used[256];
  uint8_t sys_reserved[256]; /* PC_RESERVED slots: occupied, but never matched by colour */
  uint32_t pal_gen; /* bumped when the system palette changes */
  uint32_t screen_off;
  int32_t screen_w, screen_h;
  uint16_t stock[17]; /* GetStockObject ids 0..16 */
  uint16_t default_bitmap; /* the 1x1 monochrome bitmap of a new memory DC */
  uint16_t default_palette;
  uint16_t pad;
} GdiState;

typedef struct PenObj {
  int16_t style, width;
  uint32_t color;
} PenObj;

typedef struct BrushObj {
  int16_t style; /* BS_SOLID, BS_HOLLOW, BS_PATTERN */
  uint8_t mono;  /* pattern is 0/1 bits (text / background colour) */
  uint8_t cache_valid;
  uint32_t color;
  uint32_t cache_gen;
  uint16_t cache_pal;
  uint16_t pad;
  uint8_t pat[64];   /* pattern brush source, 8x8, row-major */
  uint8_t cache[64]; /* realized solid/dither pattern for (cache_gen, cache_pal) */
} BrushObj;

typedef struct BitmapObj {
  int32_t w, h;
  uint8_t mono;
  uint8_t pad[3];
  /* w * h pixel bytes follow (row-major, one byte per pixel; mono bitmaps hold 0/1) */
} BitmapObj;

typedef struct PaletteObj {
  uint16_t n;
  uint16_t pad;
  /* n * 4 bytes (R, G, B, flags) then n map bytes (system index per logical entry) follow;
   * 'realized' is 0 until the first RealizePalette; gdi_palette_map maps lazily then */
  uint8_t realized;
  uint8_t pad2[3];
} PaletteObj;

typedef struct RegionObj {
  int32_t count;
  /* count RectI follow */
} RegionObj;

typedef struct DCObj {
  uint8_t memory;
  uint8_t pal_bg;
  int16_t bkmode, rop2, polyfill, stretch;
  uint16_t hbitmap; /* memory DC: selected bitmap */
  uint16_t hpen, hbrush, hpal, hfont;
  uint32_t bkcolor, textcolor;
  int32_t curx, cury;
  int32_t wox, woy; /* window origin */
  int32_t vox, voy; /* device origin on the screen */
  int32_t box, boy; /* brush origin */
  uint32_t clip_off; /* RegionObj (device coordinates, relative to the device origin), 0 = none */
  RectI hard;        /* hard clip, screen coordinates (screen DCs) */
} DCObj;

/* A drawing surface: 8-bit pixels (indices; mono surfaces hold 0/1). */
typedef struct Surf {
  uint8_t *p;
  int32_t w, h;
  int mono;
} Surf;

GdiState *gdi_state(void);
void gdi_obj_ref(uint16_t h, int delta); /* adjusts the selection count of an object */
DCObj *gdi_dc_get(HDC16 h);
bool dc_surface(const DCObj *dc, Surf *s);

/* Colours. */
uint8_t gdi_pen_index(const DCObj *dc, COLORREF16 c);  /* nearest (pens, text, SetPixel) */
void gdi_brush_pattern(const DCObj *dc, BrushObj *b, uint8_t out[64]); /* realized 8x8 */
uint8_t gdi_sys_nearest(int r, int g, int b);          /* among used, non-reserved system entries */
int gdi_sys_exact(int r, int g, int b);                /* same set; -1 if none */
uint8_t gdi_static_nearest(int r, int g, int b);       /* among the 20 static colours only */
int gdi_static_exact(int r, int g, int b);             /* the 20 statics only; -1 if none */
uint8_t gdi_dib_index(const DCObj *dc, int r, int g, int b); /* DIB colour -> pixel (whole palette) */
uint8_t gdi_mono_index(int r, int g, int b);
void gdi_colorref_rgb(const DCObj *dc, COLORREF16 c, int rgb[3]); /* what the colour looks like */

/* Clip. */
bool gdi_clip_contains(const DCObj *dc, int32_t devx, int32_t devy);
/* Visible test for one pixel in surface coordinates (surface bounds, hard clip, clip region). */
bool gdi_visible(const DCObj *dc, const Surf *s, int32_t sx, int32_t sy);

/* Coordinate conversion: logical -> surface. */
static inline int32_t gdi_sx(const DCObj *dc, int32_t x) { return x - dc->wox + dc->vox; }
static inline int32_t gdi_sy(const DCObj *dc, int32_t y) { return y - dc->woy + dc->voy; }

void gdi_clip_free(DCObj *dc);

/* Raster helpers (gdi_blit.c). */
uint8_t gdi_apply_rop2(int n, uint8_t p, uint8_t d);
/* Writes one surface pixel (already visible-tested) with the DC's ROP2 and a pen index. */
void gdi_put_rop2(const DCObj *dc, Surf *s, int32_t sx, int32_t sy, uint8_t pen_index);

/* framebuffer.c: system palette and screen. */
void fb_init_palette(GdiState *g);
int fb_alloc_screen(GdiState *g);

/* Palette helpers. */
PaletteObj *gdi_palette_get(uint16_t h);
uint8_t *gdi_palette_entries(PaletteObj *p);
uint8_t *gdi_palette_map(PaletteObj *p);

#endif /* STARS_RUNTIME_GDI_INTERNAL_H */
