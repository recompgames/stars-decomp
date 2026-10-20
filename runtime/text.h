/* Fonts and text output (task stars-0z4.5): CreateFontIndirect, TextOut, ExtTextOut,
 * GetTextExtent, GetTextMetrics (GDI) and DrawText (USER), on the raster fonts of runtime/fnt.h.
 * Plain C with Win16 names and types; the harness adapter is harness/text_thunks.c.
 *
 * Fonts: no Microsoft font file is used. The built-in set is rasterised from the OFL-licensed
 * Liberation Sans (runtime/fontdata.c, tools/genfont.c, third_party/fonts/README.md): "System",
 * "MS Sans Serif" 8 pt and "Arial" in regular and bold at several sizes. Glyph shapes and widths
 * are look-alikes and every pixel expectation is UNVERIFIED against Windows 3.1.
 *
 * Font mapping (CreateFontIndirect / text_map_logfont), Windows 3.1 rule recalled from memory,
 * UNVERIFIED: the face name picks the family (case-insensitive; a trailing " Bold", " Italic" or
 * " Bold Italic" on a name that is not a known family is stripped: Bold asks for the bold weight,
 * Italic sets tmItalic (glyphs are not slanted), "Helv" and "Helvetica" are "MS Sans Serif"; an empty or unknown face falls back to
 * "System"). Within the family, regular or bold is chosen by lfWeight >= 600 (the other weight is
 * used only when the family has no font of the wanted weight). Then the largest font not taller
 * than the request is taken, or the smallest one when all are taller. The request is lfHeight < 0:
 * character height (pixHeight - internalLeading), lfHeight > 0: cell height (pixHeight), 0: 13.
 * lfEscapement/lfOrientation (rotated text) are ignored: raster fonts cannot rotate. */
#ifndef STARS_RUNTIME_TEXT_H
#define STARS_RUNTIME_TEXT_H

#include <stddef.h>
#include <stdint.h>

#include "runtime/fnt.h"
#include "runtime/gdi.h"

typedef uint16_t HFONT16;

/* ExtTextOut options. */
#define ETO_GRAYED 0x0001
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004

/* DrawText flags. */
#define DT_TOP 0x0000
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_RIGHT 0x0002
#define DT_VCENTER 0x0004
#define DT_BOTTOM 0x0008
#define DT_WORDBREAK 0x0010
#define DT_SINGLELINE 0x0020
#define DT_EXPANDTABS 0x0040
#define DT_TABSTOP 0x0080
#define DT_NOCLIP 0x0100
#define DT_EXTERNALLEADING 0x0200
#define DT_CALCRECT 0x0400
#define DT_NOPREFIX 0x0800

/* Win16 LOGFONT (50 bytes) and TEXTMETRIC (31 bytes, packed) layouts. */
#define LOGFONT16_SIZE 50
#define LF_HEIGHT 0
#define LF_WIDTH 2
#define LF_ESCAPEMENT 4
#define LF_ORIENTATION 6
#define LF_WEIGHT 8
#define LF_ITALIC 10
#define LF_UNDERLINE 11
#define LF_STRIKEOUT 12
#define LF_CHARSET 13
#define LF_PITCHFAMILY 17
#define LF_FACENAME 18
#define LF_FACENAME_LEN 32
#define TEXTMETRIC16_SIZE 31

/* Called by gdi_init: registers the built-in fonts, the stock fonts (OEM_FIXED_FONT ...
 * SYSTEM_FIXED_FONT) and the SelectObject font hook. Resets fonts added with
 * text_register_font. Returns 0 on success. */
int text_init(void);

/* Adds a FNT 2.0/3.0 resource (the buffer must outlive the runtime) to the font set; returns its
 * font index, or negative when the resource is malformed. For tests and later .FON loading. */
int text_register_font(const uint8_t *fnt, size_t size);

/* Font set access (host-side, for tests and tools). */
int text_font_count(void);
const FntFont *text_font_info(int index);
/* The mapping rule above for a 50-byte LOGFONT image; returns the font index. */
int text_map_logfont(const uint8_t *logfont);

HFONT16 CreateFontIndirect(FARPTR lplf);
uint16_t TextOut(HDC16 hdc, int16_t x, int16_t y, FARPTR str, int16_t count);
uint16_t ExtTextOut(HDC16 hdc, int16_t x, int16_t y, uint16_t options, FARPTR rect, FARPTR str,
                    uint16_t count, FARPTR dx);
/* Returns the extent as a DWORD: width in the low word, height in the high word. */
uint32_t GetTextExtent(HDC16 hdc, FARPTR str, int16_t count);
uint16_t GetTextMetrics(HDC16 hdc, FARPTR lptm);
int16_t DrawText(HDC16 hdc, FARPTR str, int16_t count, FARPTR rect, uint16_t flags);

#endif
