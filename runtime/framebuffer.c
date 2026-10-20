/* Screen surface and system palette (task stars-0z4.4). 800x600, 8-bpp indexed, the 20
 * Windows static colours at 0-9 and 246-255. All state is in the
 * GdiState block of the arena. Plain C: no harness dependency. */
#include <limits.h>
#include <string.h>

#include "runtime/gdi_internal.h"

/* The 20 static colours in Windows order: 0-9 are system indices 0-9, 10-19 are 246-255. */
static const uint8_t k_static[20][3] = {
    {0, 0, 0},       {128, 0, 0},   {0, 128, 0},     {128, 128, 0},   {0, 0, 128},
    {128, 0, 128},   {0, 128, 128}, {192, 192, 192}, {192, 220, 192}, {166, 202, 240},
    {255, 251, 240}, {160, 160, 164}, {128, 128, 128}, {255, 0, 0},   {0, 255, 0},
    {255, 255, 0},   {0, 0, 255},   {255, 0, 255},   {0, 255, 255},  {255, 255, 255}};

void fb_init_palette(GdiState *g) {
  memset(g->sys_rgb, 0, sizeof g->sys_rgb);
  memset(g->sys_used, 0, sizeof g->sys_used);
  memset(g->sys_reserved, 0, sizeof g->sys_reserved);
  for (int i = 0; i < 20; i++) {
    int idx = i < 10 ? i : 246 + (i - 10);
    memcpy(g->sys_rgb[idx], k_static[i], 3);
    g->sys_used[idx] = 1;
  }
}

int fb_alloc_screen(GdiState *g) {
  g->screen_w = GDI_SCREEN_W;
  g->screen_h = GDI_SCREEN_H;
  void *p = rt_alloc((size_t)g->screen_w * (size_t)g->screen_h);
  if (!p) return -1;
  memset(p, 0, (size_t)g->screen_w * (size_t)g->screen_h);
  g->screen_off = mem_off(p);
  return 0;
}

int fb_width(void) {
  GdiState *g = gdi_state();
  return g ? g->screen_w : 0;
}

int fb_height(void) {
  GdiState *g = gdi_state();
  return g ? g->screen_h : 0;
}

uint8_t *fb_pixels(void) {
  GdiState *g = gdi_state();
  return g ? mem_arena() + g->screen_off : NULL;
}

COLORREF16 fb_palette_entry(unsigned i) {
  GdiState *g = gdi_state();
  if (!g || i >= 256 || !g->sys_used[i]) return 0;
  return GDI_RGB(g->sys_rgb[i][0], g->sys_rgb[i][1], g->sys_rgb[i][2]);
}

void fb_present(uint32_t *out) {
  GdiState *g = gdi_state();
  if (!g) return;
  uint32_t lut[256];
  for (int i = 0; i < 256; i++)
    lut[i] = (uint32_t)g->sys_rgb[i][0] | ((uint32_t)g->sys_rgb[i][1] << 8) |
             ((uint32_t)g->sys_rgb[i][2] << 16) | 0xFF000000u;
  const uint8_t *src = mem_arena() + g->screen_off;
  size_t n = (size_t)g->screen_w * (size_t)g->screen_h;
  for (size_t i = 0; i < n; i++) out[i] = lut[src[i]];
}

/* ---- colour matching ---- */

static const uint8_t k_static_idx[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};

/* Plain RGB colours (pens, text, SetPixel, solid brushes) match the 20 static colours only. */
int gdi_static_exact(int r, int gr, int b) {
  GdiState *g = gdi_state();
  for (int k = 0; k < 20; k++) {
    int i = k_static_idx[k];
    if (g->sys_rgb[i][0] == r && g->sys_rgb[i][1] == gr && g->sys_rgb[i][2] == b) return i;
  }
  return -1;
}

uint8_t gdi_static_nearest(int r, int gr, int b) {
  GdiState *g = gdi_state();
  long best = LONG_MAX;
  int bi = 0;
  for (int k = 0; k < 20; k++) {
    int i = k_static_idx[k];
    long dr = g->sys_rgb[i][0] - r, dg = g->sys_rgb[i][1] - gr, db = g->sys_rgb[i][2] - b;
    long d = dr * dr + dg * dg + db * db;
    if (d < best) { best = d; bi = i; }
  }
  return (uint8_t)bi;
}

/* Matching among the used system entries (statics and realized colours); PC_RESERVED slots
 * are never matched. */

int gdi_sys_exact(int r, int gr, int b) {
  GdiState *g = gdi_state();
  for (int i = 0; i < 256; i++)
    if (g->sys_used[i] && !g->sys_reserved[i] && g->sys_rgb[i][0] == r && g->sys_rgb[i][1] == gr && g->sys_rgb[i][2] == b) return i;
  return -1;
}

uint8_t gdi_sys_nearest(int r, int gr, int b) {
  GdiState *g = gdi_state();
  long best = LONG_MAX;
  int bi = 0;
  for (int i = 0; i < 256; i++) {
    if (!g->sys_used[i] || g->sys_reserved[i]) continue;
    long dr = g->sys_rgb[i][0] - r, dg = g->sys_rgb[i][1] - gr, db = g->sys_rgb[i][2] - b;
    long d = dr * dr + dg * dg + db * db;
    if (d < best) { best = d; bi = i; }
  }
  return (uint8_t)bi;
}

uint8_t gdi_mono_index(int r, int g, int b) { return (uint8_t)((r * 30 + g * 59 + b * 11) / 100 >= 128 ? 1 : 0); }
