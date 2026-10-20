/* Native-only adapter: registers the text functions of runtime/text.h (GDI TextOut, ExtTextOut,
 * GetTextExtent, GetTextMetrics, CreateFontIndirect and USER DrawText) with the harness
 * dispatcher under (module, ordinal). gdi_register() calls it. Built into libharness.a only. */
#ifndef STARS_HARNESS_TEXT_THUNKS_H
#define STARS_HARNESS_TEXT_THUNKS_H

void text_register(void);

#endif
