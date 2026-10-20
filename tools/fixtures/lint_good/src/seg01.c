/* Fixture: a clean decompiled segment. */
#include <stdint.h>
#include "runtime/frame.h"

static const uint16_t k_tab[2] = {1, 2};

DECOMP(1, 0x0000, fx_add, FAR_CDECL)
int16_t fx_add(int16_t a)
{
  uint16_t x = (uint16_t)(a + k_tab[0]);
  /* int in a comment, and "long" in a string, are ignored */
  (void)CALL_ORIG(1, 0x0010, FAR_CDECL, 0);
  return (int16_t)x;
}
