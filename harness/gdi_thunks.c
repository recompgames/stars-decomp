/* HCall decoders for the GDI core of runtime/gdi.h (task stars-0z4.4), plus the two USER
 * palette functions the GDI core owns. Native builds only. Arguments are decoded from the
 * PASCAL stack words: hcall_word(c, 0) is the LAST pushed word, and a DWORD or far pointer is
 * pushed high word first, so its low word is word n and its high word word n+1. */
#include "harness/gdi_thunks.h"

#include "harness/harness.h"
#include "harness/text_thunks.h"
#include "runtime/gdi.h"

static uint32_t w32(const HCall *c, unsigned n) {
  return (uint32_t)hcall_word(c, n) | ((uint32_t)hcall_word(c, n + 1) << 16);
}
static int16_t s16(const HCall *c, unsigned n) { return (int16_t)hcall_word(c, n); }
#define W(n) hcall_word(c, (n))
#define S(n) s16(c, (n))

static void t_SetBkColor(HCall *c) { c->ret = SetBkColor(W(2), w32(c, 0)); }
static void t_SetBkMode(HCall *c) { c->ret = (uint16_t)SetBkMode(W(1), S(0)); }
static void t_SetROP2(HCall *c) { c->ret = (uint16_t)SetROP2(W(1), S(0)); }
static void t_GetROP2(HCall *c) { c->ret = (uint16_t)GetROP2(W(0)); }
static void t_GetBkColor(HCall *c) { c->ret = GetBkColor(W(0)); }
static void t_SetTextColor(HCall *c) { c->ret = SetTextColor(W(2), w32(c, 0)); }
static void t_SetWindowOrg(HCall *c) { c->ret = SetWindowOrg(W(2), S(1), S(0)); }
static void t_SetBrushOrg(HCall *c) { c->ret = SetBrushOrg(W(2), S(1), S(0)); }
static void t_MoveTo(HCall *c) { c->ret = MoveTo(W(2), S(1), S(0)); }
static void t_LineTo(HCall *c) { c->ret = LineTo(W(2), S(1), S(0)); }
static void t_Rectangle(HCall *c) { c->ret = Rectangle(W(4), S(3), S(2), S(1), S(0)); }
static void t_Ellipse(HCall *c) { c->ret = Ellipse(W(4), S(3), S(2), S(1), S(0)); }
static void t_PatBlt(HCall *c) { c->ret = PatBlt(W(6), S(5), S(4), S(3), S(2), w32(c, 0)); }
static void t_SetPixel(HCall *c) { c->ret = SetPixel(W(4), S(3), S(2), w32(c, 0)); }
static void t_BitBlt(HCall *c) {
  c->ret = BitBlt(W(9), S(8), S(7), S(6), S(5), W(4), S(3), S(2), w32(c, 0));
}
static void t_StretchDIBits(HCall *c) {
  c->ret = (uint16_t)StretchDIBits(W(15), S(14), S(13), S(12), S(11), S(10), S(9), S(8), S(7),
                                   w32(c, 5), w32(c, 3), W(2), w32(c, 0));
}
static void t_GetDIBits(HCall *c) {
  c->ret = (uint16_t)GetDIBits(W(8), W(7), W(6), W(5), w32(c, 3), w32(c, 1), W(0));
}
static void t_ExcludeClipRect(HCall *c) {
  c->ret = (uint16_t)ExcludeClipRect(W(4), S(3), S(2), S(1), S(0));
}
static void t_IntersectClipRect(HCall *c) {
  c->ret = (uint16_t)IntersectClipRect(W(4), S(3), S(2), S(1), S(0));
}
static void t_SelectClipRgn(HCall *c) { c->ret = (uint16_t)SelectClipRgn(W(1), W(0)); }
static void t_CreateRectRgn(HCall *c) { c->ret = CreateRectRgn(S(3), S(2), S(1), S(0)); }
static void t_SelectObject(HCall *c) { c->ret = SelectObject(W(1), W(0)); }
static void t_CreateCompatibleBitmap(HCall *c) { c->ret = CreateCompatibleBitmap(W(2), S(1), S(0)); }
static void t_CreateCompatibleDC(HCall *c) { c->ret = CreateCompatibleDC(W(0)); }
static void t_CreatePatternBrush(HCall *c) { c->ret = CreatePatternBrush(W(0)); }
static void t_CreatePen(HCall *c) { c->ret = CreatePen(S(3), S(2), w32(c, 0)); }
static void t_CreateSolidBrush(HCall *c) { c->ret = CreateSolidBrush(w32(c, 0)); }
static void t_DeleteDC(HCall *c) { c->ret = DeleteDC(W(0)); }
static void t_DeleteObject(HCall *c) { c->ret = DeleteObject(W(0)); }
static void t_GetObject(HCall *c) { c->ret = (uint16_t)GetObject(W(3), S(2), w32(c, 0)); }
static void t_GetStockObject(HCall *c) { c->ret = GetStockObject(S(0)); }
static void t_UnrealizeObject(HCall *c) { c->ret = UnrealizeObject(W(0)); }
static void t_CreatePalette(HCall *c) { c->ret = CreatePalette(w32(c, 0)); }
static void t_GetDeviceCaps(HCall *c) { c->ret = (uint16_t)GetDeviceCaps(W(1), S(0)); }
static void t_MulDiv(HCall *c) { c->ret = (uint16_t)MulDiv(S(2), S(1), S(0)); }
static void t_Escape(HCall *c) { c->ret = (uint16_t)Escape(W(6), S(5), S(4), w32(c, 2), w32(c, 0)); }
static void t_SelectPalette(HCall *c) { c->ret = SelectPalette(W(2), W(1), W(0)); }
static void t_RealizePalette(HCall *c) { c->ret = RealizePalette(W(0)); }

static const struct {
  const char *module;
  uint16_t ordinal;
  HApiFn fn;
} g_apis[] = {
    {"GDI", 1, t_SetBkColor},      {"GDI", 2, t_SetBkMode},
    {"GDI", 4, t_SetROP2},         {"GDI", 9, t_SetTextColor},
    {"GDI", 11, t_SetWindowOrg},   {"GDI", 19, t_LineTo},
    {"GDI", 20, t_MoveTo},         {"GDI", 21, t_ExcludeClipRect},
    {"GDI", 22, t_IntersectClipRect}, {"GDI", 24, t_Ellipse},
    {"GDI", 27, t_Rectangle},      {"GDI", 29, t_PatBlt},
    {"GDI", 31, t_SetPixel},       {"GDI", 34, t_BitBlt},
    {"GDI", 38, t_Escape},         {"GDI", 44, t_SelectClipRgn},
    {"GDI", 45, t_SelectObject},   {"GDI", 51, t_CreateCompatibleBitmap},
    {"GDI", 52, t_CreateCompatibleDC}, {"GDI", 60, t_CreatePatternBrush},
    {"GDI", 61, t_CreatePen},      {"GDI", 64, t_CreateRectRgn},
    {"GDI", 66, t_CreateSolidBrush}, {"GDI", 68, t_DeleteDC},
    {"GDI", 69, t_DeleteObject},   {"GDI", 75, t_GetBkColor},
    {"GDI", 80, t_GetDeviceCaps},  {"GDI", 82, t_GetObject},
    {"GDI", 85, t_GetROP2},        {"GDI", 87, t_GetStockObject},
    {"GDI", 128, t_MulDiv},        {"GDI", 148, t_SetBrushOrg},
    {"GDI", 150, t_UnrealizeObject}, {"GDI", 360, t_CreatePalette},
    {"GDI", 439, t_StretchDIBits}, {"GDI", 441, t_GetDIBits},
    {"USER", 282, t_SelectPalette}, {"USER", 283, t_RealizePalette},
};

void gdi_register(void) {
  for (size_t i = 0; i < sizeof g_apis / sizeof g_apis[0]; i++)
    harness_register(g_apis[i].module, g_apis[i].ordinal, g_apis[i].fn);
  text_register(); /* GDI text functions and USER DrawText (runtime/text.h) */
}
