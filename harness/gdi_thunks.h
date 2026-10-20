/* Native-only adapter: registers the plain GDI functions of runtime/gdi.h (and the two USER
 * palette functions) with the harness dispatcher under (module, ordinal). Built into
 * libharness.a only. */
#ifndef STARS_HARNESS_GDI_THUNKS_H
#define STARS_HARNESS_GDI_THUNKS_H

/* Registers every GDI core function with harness_register. Call after gdi_init(). */
void gdi_register(void);

#endif /* STARS_HARNESS_GDI_THUNKS_H */
