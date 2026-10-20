/* GDI core (task stars-0z4.4): framebuffer, DCs, objects, raster operations, primitives,
 * bitmaps, clipping and palettes, as plain C functions named and typed like the Win16 API.
 * No harness dependency: the native harness decodes the PASCAL stack into these calls in
 * harness/gdi_thunks.c (gdi_register). Far pointers are FARPTR (selector:offset).
 *
 * Display: 800x600, 8-bpp indexed, the 20 static colours plus
 * logical palettes. All GDI state lives in the arena (runtime/mem.h rt_alloc), so a snapshot
 * of the arena is a snapshot of GDI. Text functions belong to the fonts task; they use the
 * hooks and object helpers at the end of this file.
 *
 * Coordinates: logical -> device is "minus window origin" (MM_TEXT only). A DC has a device
 * origin (the window client position on the screen, set by USER) and a hard clip rectangle in
 * screen coordinates; the clip region of ExcludeClipRect & co. is in DC device coordinates.
 */
#ifndef STARS_RUNTIME_GDI_H
#define STARS_RUNTIME_GDI_H

#include <stdbool.h>
#include <stdint.h>

#include "runtime/include/win16.h"

typedef uint16_t HDC16;
typedef uint16_t HGDIOBJ16;
typedef uint16_t HBITMAP16;
typedef uint16_t HBRUSH16;
typedef uint16_t HPEN16;
typedef uint16_t HPALETTE16;
typedef uint16_t HRGN16;
typedef uint32_t COLORREF16;

/* Win16 RECT is 4 x int16. */
typedef struct RECT16 {
  int16_t left, top, right, bottom;
} RECT16;

#define GDI_RGB(r, g, b) ((COLORREF16)((uint8_t)(r) | ((uint32_t)(uint8_t)(g) << 8) | ((uint32_t)(uint8_t)(b) << 16)))
#define GDI_PALETTEINDEX(i) ((COLORREF16)(0x01000000UL | (uint16_t)(i)))
#define GDI_PALETTERGB(r, g, b) ((COLORREF16)(0x02000000UL | GDI_RGB(r, g, b)))

/* Display mode. */
#define GDI_SCREEN_W 800
#define GDI_SCREEN_H 600
#define GDI_NUM_RESERVED 20
#define GDI_SIZE_PALETTE 256

/* Ternary raster operations (the high byte of the 32-bit code is the truth table). */
#define BLACKNESS 0x00000042UL
#define NOTSRCERASE 0x001100A6UL
#define NOTSRCCOPY 0x00330008UL
#define SRCERASE 0x00440328UL
#define DSTINVERT 0x00550009UL
#define PATINVERT 0x005A0049UL
#define SRCINVERT 0x00660046UL
#define SRCAND 0x008800C6UL
#define MERGEPAINT 0x00BB0226UL
#define MERGECOPY 0x00C000CAUL
#define SRCCOPY 0x00CC0020UL
#define SRCPAINT 0x00EE0086UL
#define PATCOPY 0x00F00021UL
#define PATPAINT 0x00FB0A09UL
#define WHITENESS 0x00FF0062UL

/* Binary raster operations. */
#define R2_BLACK 1
#define R2_NOTMERGEPEN 2
#define R2_MASKNOTPEN 3
#define R2_NOTCOPYPEN 4
#define R2_MASKPENNOT 5
#define R2_NOT 6
#define R2_XORPEN 7
#define R2_NOTMASKPEN 8
#define R2_MASKPEN 9
#define R2_NOTXORPEN 10
#define R2_NOP 11
#define R2_MERGENOTPEN 12
#define R2_COPYPEN 13
#define R2_MERGEPENNOT 14
#define R2_MERGEPEN 15
#define R2_WHITE 16

#define TRANSPARENT 1
#define OPAQUE 2

#define PS_SOLID 0
#define PS_DASH 1
#define PS_DOT 2
#define PS_DASHDOT 3
#define PS_DASHDOTDOT 4
#define PS_NULL 5
#define PS_INSIDEFRAME 6

#define BS_SOLID 0
#define BS_HOLLOW 1
#define BS_NULL BS_HOLLOW
#define BS_HATCHED 2
#define BS_PATTERN 3

#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define HOLLOW_BRUSH NULL_BRUSH
#define WHITE_PEN 6
#define BLACK_PEN 7
#define NULL_PEN 8
#define OEM_FIXED_FONT 10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT 12
#define SYSTEM_FONT 13
#define DEVICE_DEFAULT_FONT 14
#define DEFAULT_PALETTE 15
#define SYSTEM_FIXED_FONT 16

/* Region results. */
#define GDI_ERROR_REGION 0
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3

/* GetDeviceCaps indices. */
#define DRIVERVERSION 0
#define TECHNOLOGY 2
#define HORZSIZE 4
#define VERTSIZE 6
#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define PLANES 14
#define NUMBRUSHES 16
#define NUMPENS 18
#define NUMMARKERS 20
#define NUMFONTS 22
#define NUMCOLORS 24
#define PDEVICESIZE 26
#define CURVECAPS 28
#define LINECAPS 30
#define POLYGONALCAPS 32
#define TEXTCAPS 34
#define CLIPCAPS 36
#define RASTERCAPS 38
#define ASPECTX 40
#define ASPECTY 42
#define ASPECTXY 44
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define SIZEPALETTE 104
#define NUMRESERVED 106
#define COLORRES 108

/* DIB colour usage. */
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1

/* Palette entry flags (PALETTEENTRY.peFlags). */
#define PC_RESERVED 1
#define PC_EXPLICIT 2
#define PC_NOCOLLAPSE 4

/* Escape codes seen at call sites (all return 0 until the printing task). */
#define GDI_NEWFRAME 1
#define GDI_STARTDOC 10
#define GDI_ENDDOC 11

/* ---- lifecycle ---- */

/* Builds the GDI state in the arena (call after mem_init / harness_init). Safe to call again
 * after gdi_shutdown. Returns 0 on success, negative on failure. */
int gdi_init(void);
/* Frees every GDI block. Call while the arena is alive (before mem_shutdown). */
void gdi_shutdown(void);

/* ---- framebuffer (runtime/framebuffer.c) ---- */

int fb_width(void);
int fb_height(void);
/* Host pointer to the 8-bpp screen (fb_width() * fb_height() bytes); valid until gdi_shutdown. */
uint8_t *fb_pixels(void);
/* Converts the screen through the system palette into out[fb_width() * fb_height()] as RGBA
 * bytes R,G,B,255 (a little-endian uint32 0xFFBBGGRR), top row first. */
void fb_present(uint32_t *out);
/* System palette entry i as GDI_RGB; 0 for an unused slot. */
COLORREF16 fb_palette_entry(unsigned i);

/* ---- helpers for USER (window DCs) and the fonts task ---- */

/* A DC for the screen surface (what GetDC/BeginPaint wrap). DeleteDC releases it. */
HDC16 gdi_create_screen_dc(void);
/* Screen position of the DC's device origin and the hard clip rectangle (screen coordinates,
 * right/bottom exclusive). Defaults: origin 0,0 and the whole screen. */
void gdi_dc_set_origin(HDC16 hdc, int16_t x, int16_t y);
void gdi_dc_set_hard_clip(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b);

/* Object kinds for the generic table (fonts and others register their own payloads). */
enum { GDI_OBJ_NONE = 0, GDI_OBJ_PEN, GDI_OBJ_BRUSH, GDI_OBJ_BITMAP, GDI_OBJ_PALETTE, GDI_OBJ_REGION, GDI_OBJ_DC, GDI_OBJ_FONT };
/* Allocates a handle for a payload of the given size (zeroed, in the arena). 0 on failure. */
uint16_t gdi_obj_alloc(int type, uint32_t payload_bytes);
/* Host pointer to the payload when h is a live object of that type, else NULL. */
void *gdi_obj_get(uint16_t h, int type);
/* Frees an object regardless of selection (used by owners of custom payloads). */
bool gdi_obj_free(uint16_t h);
/* Registers a stock object handle (fonts: OEM_FIXED_FONT ... SYSTEM_FIXED_FONT). */
void gdi_set_stock(int id, uint16_t handle);
/* Font selection hook: SelectObject on a GDI_OBJ_FONT calls it and returns its result. */
typedef uint16_t (*GdiSelectFontFn)(HDC16 hdc, uint16_t hfont);
void gdi_set_font_hook(GdiSelectFontFn fn);
/* DC attribute readers for text drawing. */
COLORREF16 GetTextColor(HDC16 hdc);
int16_t GetBkMode(HDC16 hdc);
/* Palette index (or mono 0/1) that a COLORREF draws as on this DC's surface. */
uint8_t gdi_dc_color_index(HDC16 hdc, COLORREF16 c);
/* Host pointer to a bitmap's pixels (one byte per pixel, row-major, width*height bytes; mono
 * bitmaps hold 0/1). NULL when h is not a bitmap. Valid until the bitmap is deleted. */
uint8_t *gdi_bitmap_bits(HBITMAP16 h, int *w, int *hgt, bool *mono);
/* Plots one device pixel (DC logical coordinates) honouring clipping, with SRCCOPY. */
bool gdi_dc_put_pixel(HDC16 hdc, int16_t x, int16_t y, uint8_t index);

/* ---- raster operation evaluators (exported for tests and the fonts task) ---- */

/* Evaluates the operation-index byte (rop >> 16) bitwise on pattern, source and destination
 * bytes: result bit = index bit ((P << 2) | (S << 1) | D). */
uint8_t gdi_rop3(uint32_t rop, uint8_t p, uint8_t s, uint8_t d);
/* Binary raster operation n (R2_*, 1..16) bitwise on pen and destination bytes. */
uint8_t gdi_rop2(int n, uint8_t p, uint8_t d);

/* ---- GDI API (Win16 names and types) ---- */

uint32_t SetBkColor(HDC16 hdc, COLORREF16 c); /* returns the old colour */
int16_t SetBkMode(HDC16 hdc, int16_t mode);
int16_t SetROP2(HDC16 hdc, int16_t mode);
int16_t GetROP2(HDC16 hdc);
uint32_t GetBkColor(HDC16 hdc);
uint32_t SetTextColor(HDC16 hdc, COLORREF16 c);
/* Origins return the old origin as DWORD: x in the low word, y in the high word. */
uint32_t SetWindowOrg(HDC16 hdc, int16_t x, int16_t y);
uint32_t SetBrushOrg(HDC16 hdc, int16_t x, int16_t y);

uint32_t MoveTo(HDC16 hdc, int16_t x, int16_t y);
uint16_t LineTo(HDC16 hdc, int16_t x, int16_t y);
uint16_t Rectangle(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b);
uint16_t Ellipse(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b);
uint16_t PatBlt(HDC16 hdc, int16_t x, int16_t y, int16_t w, int16_t h, uint32_t rop);
uint32_t SetPixel(HDC16 hdc, int16_t x, int16_t y, COLORREF16 c);
uint16_t BitBlt(HDC16 hdcDest, int16_t x, int16_t y, int16_t w, int16_t h, HDC16 hdcSrc, int16_t xs,
                int16_t ys, uint32_t rop);
int16_t StretchDIBits(HDC16 hdc, int16_t xd, int16_t yd, int16_t cxd, int16_t cyd, int16_t xs,
                      int16_t ys, int16_t cxs, int16_t cys, FARPTR bits, FARPTR bmi, uint16_t usage,
                      uint32_t rop);
int16_t GetDIBits(HDC16 hdc, HBITMAP16 hbm, uint16_t start, uint16_t num, FARPTR bits, FARPTR bmi,
                  uint16_t usage);

int16_t ExcludeClipRect(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b);
int16_t IntersectClipRect(HDC16 hdc, int16_t l, int16_t t, int16_t r, int16_t b);
int16_t SelectClipRgn(HDC16 hdc, HRGN16 hrgn);
HRGN16 CreateRectRgn(int16_t l, int16_t t, int16_t r, int16_t b);

HGDIOBJ16 SelectObject(HDC16 hdc, HGDIOBJ16 h);
HBITMAP16 CreateCompatibleBitmap(HDC16 hdc, int16_t w, int16_t h);
HDC16 CreateCompatibleDC(HDC16 hdc);
HBRUSH16 CreatePatternBrush(HBITMAP16 hbm);
HPEN16 CreatePen(int16_t style, int16_t width, COLORREF16 c);
HBRUSH16 CreateSolidBrush(COLORREF16 c);
uint16_t DeleteDC(HDC16 hdc);
uint16_t DeleteObject(HGDIOBJ16 h);
int16_t GetObject(HGDIOBJ16 h, int16_t cb, FARPTR lp);
HGDIOBJ16 GetStockObject(int16_t id);
uint16_t UnrealizeObject(HGDIOBJ16 h);
HPALETTE16 CreatePalette(FARPTR logpalette);
HPALETTE16 SelectPalette(HDC16 hdc, HPALETTE16 hpal, uint16_t background);
uint16_t RealizePalette(HDC16 hdc);

int16_t GetDeviceCaps(HDC16 hdc, int16_t index);
int16_t MulDiv(int16_t a, int16_t b, int16_t c);
int16_t Escape(HDC16 hdc, int16_t code, int16_t cbIn, FARPTR in, FARPTR out);

#endif /* STARS_RUNTIME_GDI_H */
