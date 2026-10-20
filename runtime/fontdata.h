/* Built-in look-alike raster fonts (generated runtime/fontdata.c, tools/genfont.c). Each blob is a
 * complete Windows FNT 3.0 resource. Fonts are OFL-licensed look-alikes, see
 * third_party/fonts/README.md; no Microsoft font data is involved. */
#ifndef STARS_RUNTIME_FONTDATA_H
#define STARS_RUNTIME_FONTDATA_H

#include <stdint.h>

typedef struct FontBlob {
  const uint8_t *data;
  unsigned size;
} FontBlob;

extern const FontBlob fontdata_blobs[];
extern const unsigned fontdata_count;

#endif
