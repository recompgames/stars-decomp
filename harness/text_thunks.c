/* HCall decoders for runtime/text.h. Argument decoding follows harness/gdi_thunks.c: word n of
 * the PASCAL stack, the last pushed argument first; a far pointer or DWORD is two words, low
 * word at n and high word at n + 1. GetTextExtent (GDI 91) returns a DWORD. Signatures are
 * from tools/winespec/gdi.exe16.spec and user.exe16.spec. */
#include "harness/text_thunks.h"

#include "harness/harness.h"
#include "runtime/text.h"

static uint32_t w32(const HCall *c, unsigned n) {
  return (uint32_t)hcall_word(c, n) | ((uint32_t)hcall_word(c, n + 1) << 16);
}
#define W(n) hcall_word(c, (n))
#define S(n) ((int16_t)hcall_word(c, (n)))

/* CreateFontIndirect(ptr) */
static void t_CreateFontIndirect(HCall *c) { c->ret = CreateFontIndirect(w32(c, 0)); }
/* TextOut(word s_word s_word str word): hdc x y str count */
static void t_TextOut(HCall *c) { c->ret = TextOut(W(5), S(4), S(3), w32(c, 1), S(0)); }
/* GetTextExtent(word ptr s_word): hdc str count */
static void t_GetTextExtent(HCall *c) { c->ret = GetTextExtent(W(3), w32(c, 1), S(0)); }
/* GetTextMetrics(word ptr) */
static void t_GetTextMetrics(HCall *c) { c->ret = GetTextMetrics(W(2), w32(c, 0)); }
/* ExtTextOut(word s_word s_word word ptr str word ptr): hdc x y options rect str count dx */
static void t_ExtTextOut(HCall *c) {
  c->ret = ExtTextOut(W(10), S(9), S(8), W(7), w32(c, 5), w32(c, 3), W(2), w32(c, 0));
}
/* DrawText(word str s_word ptr word): hdc str count rect flags */
static void t_DrawText(HCall *c) { c->ret = (uint16_t)DrawText(W(6), w32(c, 4), S(3), w32(c, 1), W(0)); }

static const struct {
  const char *module;
  uint16_t ordinal;
  HApiFn fn;
} g_apis[] = {
    {"GDI", 33, t_TextOut},       {"GDI", 57, t_CreateFontIndirect}, {"GDI", 91, t_GetTextExtent},
    {"GDI", 93, t_GetTextMetrics}, {"GDI", 351, t_ExtTextOut},        {"USER", 85, t_DrawText},
};

void text_register(void) {
  for (size_t i = 0; i < sizeof g_apis / sizeof g_apis[0]; i++)
    harness_register(g_apis[i].module, g_apis[i].ordinal, g_apis[i].fn);
}
