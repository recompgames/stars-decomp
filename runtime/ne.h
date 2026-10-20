/* NE (New Executable) loader: parses a Win16 .exe or .fon image held in memory.
 *
 * ne_parse does no file I/O. The caller owns the input buffer and may free it
 * after ne_parse returns: everything in NeFile is copied (malloc'd), so results
 * stay valid until ne_free. Segment numbers are 1-based as in the file; the
 * segment array is 0-based (segs[n - 1] is segment n).
 */
#ifndef STARS_RUNTIME_NE_H
#define STARS_RUNTIME_NE_H

#include <stddef.h>
#include <stdint.h>

enum {
  NE_OK = 0,
  NE_ERR_ARG = -1,      /* NULL pointer or other bad argument */
  NE_ERR_FORMAT = -2,   /* not an NE image, or an inconsistent field */
  NE_ERR_TRUNC = -3,    /* a table or segment extends past the buffer */
  NE_ERR_NOMEM = -4
};

/* Fixup target kinds (low two bits of the record flags). */
enum {
  NE_FIX_INTERNAL = 0,  /* w1 = target segment (low byte), w2 = target offset */
  NE_FIX_ORDINAL = 1,   /* w1 = 1-based module index, w2 = ordinal */
  NE_FIX_NAME = 2,      /* w1 = 1-based module index, w2 = offset in imported-names table */
  NE_FIX_OSFIXUP = 3    /* w1 = fixup type, w2 = unused */
};

/* Source (site) types. */
enum {
  NE_SRC_LOBYTE = 0,
  NE_SRC_SEGMENT = 2,
  NE_SRC_FAR_ADDR = 3,
  NE_SRC_OFFSET = 5
};

typedef struct NeHeader {
  uint8_t linker_major, linker_minor;
  uint16_t flags;
  uint16_t auto_data_seg;   /* 1-based */
  uint16_t heap_size, stack_size;
  uint16_t entry_cs;        /* 1-based segment of the entry point */
  uint16_t entry_ip;
  uint16_t seg_count;
  uint16_t modref_count;
  uint16_t align_shift;
} NeHeader;

typedef struct NeSegment {
  uint32_t file_off;        /* sector << align_shift */
  uint32_t length;          /* bytes in the file; a stored 0 means 65536 */
  uint16_t flags;           /* bit 0 = DATA, 0x0100 = has relocations */
  uint32_t min_alloc;       /* a stored 0 means 65536 */
} NeSegment;

/* One relocation record (kind, src, additive, w1, w2 as in the file) or one
 * expanded site (seg, site from a chain walk). Both use this layout. */
typedef struct NeFixup {
  uint16_t seg;             /* 1-based segment containing the site */
  uint16_t site;            /* offset of the site in that segment */
  uint8_t src;              /* NE_SRC_* */
  uint8_t kind;             /* NE_FIX_* */
  uint8_t additive;         /* 1 = single site, 0 = chain */
  uint16_t w1, w2;          /* meaning depends on kind, see above */
} NeFixup;

typedef struct NeExport {
  uint16_t ordinal;         /* 1-based */
  uint16_t seg;             /* 1-based; 0 for an unused ordinal */
  uint16_t off;
  uint8_t flags;
  uint8_t movable;
  char *name;               /* NULL when no name is exported for this ordinal */
} NeExport;

typedef struct NeResource {
  uint16_t type;            /* integer type ID with 0x8000 cleared; 0 when type_name is set */
  char *type_name;          /* NULL for integer types */
  uint16_t id;              /* integer ID; 0 when name is set */
  char *name;               /* NULL for integer IDs */
  uint32_t offset;          /* file offset of the data (shift applied) */
  uint32_t size;            /* bytes (length field << shift) */
  uint16_t flags;
} NeResource;

typedef struct NeFile {
  NeHeader hdr;
  char *module_name;        /* resident name, ordinal 0 */
  char *description;        /* non-resident name, ordinal 0 */

  NeSegment *segs;          /* n_segs entries */
  size_t n_segs;

  NeFixup *relocs;          /* raw records as stored in the file */
  size_t n_relocs;
  NeFixup *sites;           /* expanded: chains followed, additive records as one site */
  size_t n_sites;

  char **modules;           /* module names, index 0 = module 1 */
  size_t n_modules;

  NeExport *entries;        /* n_entries entries, entries[i].ordinal == i + 1 */
  size_t n_entries;

  NeResource *resources;
  size_t n_resources;

  uint8_t *imp_names;       /* copy of the imported-names table, for NE_FIX_NAME */
  size_t imp_names_len;
} NeFile;

typedef struct NeDialogItem {
  int16_t x, y, cx, cy;
  uint16_t id;
  uint32_t style;
  char *cls;                /* "#N" for a predefined class ordinal, else the name */
  char *text;               /* NULL when empty; "#N" for an ordinal (resource id) */
  uint8_t extra_bytes;
} NeDialogItem;

typedef struct NeDialog {
  uint32_t style;
  uint8_t item_count;
  int16_t x, y, cx, cy;
  char *menu;               /* NULL when none; "#N" for an ordinal */
  char *cls;                /* NULL when none; "#N" for an ordinal */
  char *caption;            /* NULL when empty */
  int has_font;             /* style & 0x40 (DS_SETFONT) */
  uint16_t font_size;       /* points, valid when has_font */
  char *font_name;          /* valid when has_font */
  NeDialogItem *items;
  size_t n_items;
} NeDialog;

/* Parses the NE image in buf[0..len). On success returns NE_OK and fills *out.
 * On error returns a negative NE_ERR_* code, leaves *out zeroed, and holds no memory.
 * Every offset is checked against len. */
int ne_parse(const uint8_t *buf, size_t len, NeFile *out);

/* Frees everything ne_parse allocated and zeroes *f. Safe on a zeroed struct. */
void ne_free(NeFile *f);

/* Module name for a 1-based module index, or NULL when out of range. */
const char *ne_module_name(const NeFile *f, uint16_t module);

/* Copies the name for an NE_FIX_NAME import (offset from the imported-names table) into
 * out as a NUL-terminated string. Returns NE_OK, or NE_ERR_ARG when off is out of range
 * or the name does not fit in cap. */
int ne_import_name(const NeFile *f, uint16_t off, char *out, size_t cap);

/* Export by name, or NULL. */
const NeExport *ne_find_export(const NeFile *f, const char *name);

/* Resource lookup. Returns the first match in table order, or NULL. */
const NeResource *ne_find_resource(const NeFile *f, uint16_t type, uint16_t id);
const NeResource *ne_find_resource_named(const NeFile *f, uint16_t type, const char *name);

/* Parses a DIALOG resource template (the bytes of the resource). On success fills *out;
 * free with ne_dialog_free. On error returns a negative NE_ERR_* and leaves *out zeroed. */
int ne_parse_dialog(const uint8_t *data, size_t len, NeDialog *out);
void ne_dialog_free(NeDialog *d);

#endif /* STARS_RUNTIME_NE_H */
