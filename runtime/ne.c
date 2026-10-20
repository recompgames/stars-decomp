/* NE (New Executable) loader. See ne.h for the contract. No file I/O here. */
#include "runtime/ne.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Little-endian reads. Callers check the range first. */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 1 when buf[off .. off+n) lies inside a buffer of length len. */
static int in_range(size_t len, size_t off, size_t n) {
  return off <= len && n <= len - off;
}

/* Appends to a malloc'd array of n used / cap allocated elements, doubling as needed. */
#define GROW(ptr, n, cap, T)                                                  \
  do {                                                                        \
    if ((n) == (cap)) {                                                       \
      size_t nc_ = (cap) ? (cap) * 2 : 64;                                    \
      T *np_ = realloc((ptr), nc_ * sizeof(T));                               \
      if (!np_) return NE_ERR_NOMEM;                                          \
      (ptr) = np_;                                                            \
      (cap) = nc_;                                                            \
    }                                                                         \
  } while (0)

static char *dup_bytes(const uint8_t *p, size_t n) {
  char *s = malloc(n + 1);
  if (s) {
    memcpy(s, p, n);
    s[n] = '\0';
  }
  return s;
}

/* Reads a length-prefixed (Pascal) string at buf[off]. Returns NE_OK and sets *out (malloc'd). */
static int read_pstr(const uint8_t *buf, size_t len, size_t off, char **out) {
  size_t n;
  if (!in_range(len, off, 1)) return NE_ERR_TRUNC;
  n = buf[off];
  if (!in_range(len, off + 1, n)) return NE_ERR_TRUNC;
  *out = dup_bytes(buf + off + 1, n);
  return *out ? NE_OK : NE_ERR_NOMEM;
}

/* Walks a table of length-prefixed name entries (length byte, name, uint16 ordinal)
 * until a zero length byte or the end of [off, end). Each name is attached to the
 * export with the same ordinal; ordinal 0 is passed to on_zero. */
typedef int (*name_sink)(NeFile *f, int resident, uint16_t ordinal, char *name);

static int walk_names(const uint8_t *buf, size_t len, size_t off, size_t end, NeFile *f,
                      int resident, name_sink sink) {
  size_t p = off;
  if (end > len) return NE_ERR_TRUNC;
  while (p < end) {
    size_t n = buf[p];
    uint16_t ord;
    char *name;
    int rc;
    if (n == 0) break;
    if (!in_range(end, p + 1, n + 2)) return NE_ERR_TRUNC;
    ord = rd16(buf + p + 1 + n);
    rc = read_pstr(buf, len, p, &name);
    if (rc != NE_OK) return rc;
    rc = sink(f, resident, ord, name);
    if (rc != NE_OK) return rc;
    p += n + 3;
  }
  return NE_OK;
}

static int sink_name(NeFile *f, int resident, uint16_t ord, char *name) {
  if (ord == 0) {
    /* Ordinal 0 is the module name (resident) or the description (non-resident). */
    char **slot = resident ? &f->module_name : &f->description;
    if (*slot) {
      free(name);
    } else {
      *slot = name;
    }
    return NE_OK;
  }
  if (ord <= f->n_entries && f->entries[ord - 1].name == NULL) {
    f->entries[ord - 1].name = name;
  } else {
    free(name);
  }
  return NE_OK;
}

/* Expands one relocation record into sites and appends them to f->sites. */
static int expand_record(NeFile *f, size_t *sites_cap, size_t *seg_sites, const NeFixup *rec,
                         const uint8_t *seg, size_t seg_len) {
  uint16_t off = rec->site;
  size_t steps = 0;
  if (rec->additive) {
    NeFixup s = *rec;
    if ((*seg_sites)++ >= seg_len / 2) return NE_ERR_FORMAT; /* per-segment site cap */
    GROW(f->sites, f->n_sites, *sites_cap, NeFixup);
    f->sites[f->n_sites++] = s;
    return NE_OK;
  }
  /* Chain: the word at each site holds the offset of the next site; 0xFFFF ends it. */
  for (;;) {
    NeFixup s = *rec;
    uint16_t next;
    if (steps++ > seg_len / 2) return NE_ERR_FORMAT; /* cycle */
    if (!in_range(seg_len, off, 2)) return NE_ERR_FORMAT;
    s.site = off;
    if ((*seg_sites)++ >= seg_len / 2) return NE_ERR_FORMAT; /* per-segment site cap */
    GROW(f->sites, f->n_sites, *sites_cap, NeFixup);
    f->sites[f->n_sites++] = s;
    next = rd16(seg + off);
    if (next == 0xFFFF) break;
    off = next;
  }
  return NE_OK;
}

static int parse_segments_and_relocs(const uint8_t *buf, size_t len, NeFile *f, size_t segtab,
                                     size_t shift) {
  size_t i;
  size_t relocs_cap = 0, sites_cap = 0;
  int rc;
  if (f->hdr.seg_count == 0) return NE_OK;
  f->segs = calloc(f->hdr.seg_count, sizeof(NeSegment));
  if (!f->segs) return NE_ERR_NOMEM;
  f->n_segs = f->hdr.seg_count;

  for (i = 0; i < f->n_segs; i++) {
    const uint8_t *e = buf + segtab + i * 8;
    uint16_t sector = rd16(e);
    uint16_t stored_len = rd16(e + 2);
    uint16_t flags = rd16(e + 4);
    uint16_t stored_min = rd16(e + 6);
    NeSegment *s = &f->segs[i];
    size_t disk_len = stored_len ? stored_len : 65536;
    size_t rel_off, nrec, k;
    size_t seg_sites = 0; /* expanded sites in this segment, capped at length/2 */
    uint16_t seg_no = (uint16_t)(i + 1);

    s->file_off = sector ? (uint32_t)((uint64_t)sector << shift) : 0;
    s->length = (uint32_t)disk_len;
    s->flags = flags;
    s->min_alloc = stored_min ? stored_min : 65536;
    if (sector == 0) continue; /* no file data, so no relocations */
    if (!in_range(len, s->file_off, disk_len)) return NE_ERR_TRUNC;
    if (!(flags & 0x0100)) continue;

    rel_off = s->file_off + disk_len;
    if (!in_range(len, rel_off, 2)) return NE_ERR_TRUNC;
    nrec = rd16(buf + rel_off);
    if (!in_range(len, rel_off + 2, nrec * 8)) return NE_ERR_TRUNC;
    for (k = 0; k < nrec; k++) {
      const uint8_t *r = buf + rel_off + 2 + k * 8;
      NeFixup rec;
      rec.seg = seg_no;
      rec.src = r[0];
      rec.kind = r[1] & 3;
      rec.additive = (r[1] & 4) ? 1 : 0;
      rec.site = rd16(r + 2);
      rec.w1 = rd16(r + 4);
      rec.w2 = rd16(r + 6);
      if (rec.kind == NE_FIX_INTERNAL) {
        rec.w1 &= 0x00FF; /* segment number byte; the next byte is reserved */
      }
      GROW(f->relocs, f->n_relocs, relocs_cap, NeFixup);
      f->relocs[f->n_relocs++] = rec;
      rc = expand_record(f, &sites_cap, &seg_sites, &rec, buf + s->file_off, disk_len);
      if (rc != NE_OK) return rc;
    }
  }
  return NE_OK;
}

static int parse_modules(const uint8_t *buf, size_t len, NeFile *f, size_t modref,
                         size_t impnames) {
  size_t i;
  if (f->hdr.modref_count == 0) return NE_OK;
  if (!in_range(len, modref, f->hdr.modref_count * 2)) return NE_ERR_TRUNC;
  f->modules = calloc(f->hdr.modref_count, sizeof(char *));
  if (!f->modules) return NE_ERR_NOMEM;
  f->n_modules = f->hdr.modref_count;
  for (i = 0; i < f->n_modules; i++) {
    size_t name_off = impnames + rd16(buf + modref + i * 2);
    int rc = read_pstr(buf, len, name_off, &f->modules[i]);
    if (rc != NE_OK) return rc;
  }
  return NE_OK;
}

static int parse_entries(const uint8_t *buf, size_t len, NeFile *f, size_t entry_abs,
                         size_t entry_len) {
  const uint8_t *p, *end;
  size_t entries_cap = 0;
  uint32_t ord = 1;
  if (!in_range(len, entry_abs, entry_len)) return NE_ERR_TRUNC;
  p = buf + entry_abs;
  end = p + entry_len;
  while (p < end) {
    uint8_t count = *p++;
    uint8_t ind;
    unsigned j;
    if (count == 0) break;
    if (p >= end) return NE_ERR_FORMAT;
    ind = *p++;
    for (j = 0; j < count; j++) {
      NeExport e;
      if (ord > 0xFFFF) return NE_ERR_FORMAT; /* ordinals are 16-bit */
      memset(&e, 0, sizeof e);
      e.ordinal = (uint16_t)ord;
      if (ind == 0) {
        /* Unused ordinal: no data. */
      } else if (ind == 0xFF) {
        if (end - p < 6) return NE_ERR_FORMAT;
        e.flags = p[0];
        e.movable = 1;
        e.seg = p[3];
        e.off = rd16(p + 4);
        p += 6;
      } else {
        if (end - p < 3) return NE_ERR_FORMAT;
        e.flags = p[0];
        e.seg = ind;
        e.off = rd16(p + 1);
        p += 3;
      }
      GROW(f->entries, f->n_entries, entries_cap, NeExport);
      f->entries[f->n_entries++] = e;
      ord++;
    }
  }
  return NE_OK;
}

static int parse_names(const uint8_t *buf, size_t len, NeFile *f, size_t ne, size_t resident,
                       size_t nonres_abs, size_t nonres_len) {
  int rc = walk_names(buf, len, ne + resident, len, f, 1, sink_name);
  if (rc != NE_OK) return rc;
  if (nonres_len == 0) return NE_OK;
  if (!in_range(len, nonres_abs, nonres_len)) return NE_ERR_TRUNC;
  return walk_names(buf, len, nonres_abs, nonres_abs + nonres_len, f, 0, sink_name);
}

/* Resource names are offsets from the start of the resource table. */
static int parse_resources(const uint8_t *buf, size_t len, NeFile *f, size_t rsrc) {
  size_t p, shift, res_cap = 0;
  if (rsrc == 0) return NE_OK;
  if (!in_range(len, rsrc, 2)) return NE_ERR_TRUNC;
  shift = rd16(buf + rsrc);
  if (shift > 16) return NE_ERR_FORMAT;
  if (shift == 0) shift = 9;
  p = rsrc + 2;
  for (;;) {
    uint16_t tid, count, k;
    if (!in_range(len, p, 2)) return NE_ERR_TRUNC;
    tid = rd16(buf + p);
    if (tid == 0) break;
    if (!in_range(len, p, 8)) return NE_ERR_TRUNC;
    count = rd16(buf + p + 2);
    p += 8;
    if (!in_range(len, p, (size_t)count * 12)) return NE_ERR_TRUNC;
    for (k = 0; k < count; k++) {
      const uint8_t *e = buf + p + (size_t)k * 12;
      NeResource r;
      uint64_t off_bytes = (uint64_t)rd16(e) << shift;
      uint64_t size_bytes = (uint64_t)rd16(e + 2) << shift;
      uint16_t id = rd16(e + 6);
      int rc;
      memset(&r, 0, sizeof r);
      if (off_bytes > len || size_bytes > len || !in_range(len, (size_t)off_bytes, (size_t)size_bytes)) {
        return NE_ERR_TRUNC;
      }
      r.offset = (uint32_t)off_bytes;
      r.size = (uint32_t)size_bytes;
      r.flags = rd16(e + 4);
      if (tid & 0x8000) {
        r.type = tid & 0x7FFF;
      } else {
        rc = read_pstr(buf, len, rsrc + tid, &r.type_name);
        if (rc != NE_OK) return rc;
      }
      if (id & 0x8000) {
        r.id = id & 0x7FFF;
      } else {
        int rc2 = read_pstr(buf, len, rsrc + id, &r.name);
        if (rc2 != NE_OK) {
          free(r.type_name);
          return rc2;
        }
      }
      if (f->n_resources == res_cap) {
        size_t nc = res_cap ? res_cap * 2 : 64;
        NeResource *np = realloc(f->resources, nc * sizeof(NeResource));
        if (!np) {
          free(r.type_name);
          free(r.name);
          return NE_ERR_NOMEM;
        }
        f->resources = np;
        res_cap = nc;
      }
      f->resources[f->n_resources++] = r;
    }
    p += (size_t)count * 12;
  }
  return NE_OK;
}

int ne_parse(const uint8_t *buf, size_t len, NeFile *out) {
  size_t ne, entry_abs, segtab, rsrc, shift, modref, impnames, nonres_abs, nonres_len;
  uint16_t entry_off, entry_len, resident_off;
  int rc;

  if (!out) return NE_ERR_ARG;
  memset(out, 0, sizeof *out);
  if (!buf) return NE_ERR_ARG;

  if (!in_range(len, 0, 0x40)) return NE_ERR_TRUNC;
  if (buf[0] != 'M' || buf[1] != 'Z') return NE_ERR_FORMAT;
  ne = rd32(buf + 0x3C);
  if (!in_range(len, ne, 0x40)) return NE_ERR_TRUNC;
  if (buf[ne] != 'N' || buf[ne + 1] != 'E') return NE_ERR_FORMAT;

  {
    const uint8_t *h = buf + ne;
    NeHeader *hd = &out->hdr;
    hd->linker_major = h[2];
    hd->linker_minor = h[3];
    entry_off = rd16(h + 4);
    entry_len = rd16(h + 6);
    hd->flags = rd16(h + 0x0C);
    hd->auto_data_seg = rd16(h + 0x0E);
    hd->heap_size = rd16(h + 0x10);
    hd->stack_size = rd16(h + 0x12);
    hd->entry_ip = rd16(h + 0x14);
    hd->entry_cs = rd16(h + 0x16);
    hd->seg_count = rd16(h + 0x1C);
    hd->modref_count = rd16(h + 0x1E);
    nonres_len = rd16(h + 0x20);
    segtab = ne + rd16(h + 0x22);
    rsrc = rd16(h + 0x24) ? ne + rd16(h + 0x24) : 0;
    resident_off = rd16(h + 0x26);
    modref = ne + rd16(h + 0x28);
    impnames = ne + rd16(h + 0x2A);
    nonres_abs = rd32(h + 0x2C);
    hd->align_shift = rd16(h + 0x32);
  }
  if (out->hdr.align_shift > 16) {
    rc = NE_ERR_FORMAT;
    goto fail;
  }
  shift = out->hdr.align_shift ? out->hdr.align_shift : 9;
  entry_abs = ne + entry_off;

  if (!in_range(len, segtab, (size_t)out->hdr.seg_count * 8)) {
    rc = NE_ERR_TRUNC;
    goto fail;
  }
  rc = parse_segments_and_relocs(buf, len, out, segtab, shift);
  if (rc != NE_OK) goto fail;

  /* Imported names run from their table to the entry table (the next table in the file). */
  if (entry_off == 0 || ne + entry_off <= impnames) {
    rc = NE_ERR_FORMAT;
    goto fail;
  }
  out->imp_names_len = (ne + entry_off) - impnames;
  if (!in_range(len, impnames, out->imp_names_len)) {
    rc = NE_ERR_TRUNC;
    goto fail;
  }
  out->imp_names = (uint8_t *)dup_bytes(buf + impnames, out->imp_names_len);
  if (!out->imp_names) {
    rc = NE_ERR_NOMEM;
    goto fail;
  }

  rc = parse_modules(buf, len, out, modref, impnames);
  if (rc != NE_OK) goto fail;

  rc = parse_entries(buf, len, out, entry_abs, entry_len);
  if (rc != NE_OK) goto fail;

  rc = parse_names(buf, len, out, ne, resident_off, nonres_abs, nonres_len);
  if (rc != NE_OK) goto fail;

  rc = parse_resources(buf, len, out, rsrc);
  if (rc != NE_OK) goto fail;

  return NE_OK;

fail:
  ne_free(out);
  memset(out, 0, sizeof *out);
  return rc;
}

void ne_free(NeFile *f) {
  size_t i;
  if (!f) return;
  free(f->module_name);
  free(f->description);
  free(f->segs);
  free(f->relocs);
  free(f->sites);
  for (i = 0; i < f->n_modules; i++) free(f->modules[i]);
  free(f->modules);
  for (i = 0; i < f->n_entries; i++) free(f->entries[i].name);
  free(f->entries);
  for (i = 0; i < f->n_resources; i++) {
    free(f->resources[i].type_name);
    free(f->resources[i].name);
  }
  free(f->resources);
  free(f->imp_names);
  memset(f, 0, sizeof *f);
}

const char *ne_module_name(const NeFile *f, uint16_t module) {
  if (!f || module == 0 || module > f->n_modules) return NULL;
  return f->modules[module - 1];
}

int ne_import_name(const NeFile *f, uint16_t off, char *out, size_t cap) {
  size_t n;
  if (!f || !out || cap == 0 || !f->imp_names || off >= f->imp_names_len) return NE_ERR_ARG;
  n = f->imp_names[off];
  if (off + 1 + n > f->imp_names_len || n + 1 > cap) return NE_ERR_ARG;
  memcpy(out, f->imp_names + off + 1, n);
  out[n] = '\0';
  return NE_OK;
}

const NeExport *ne_find_export(const NeFile *f, const char *name) {
  size_t i;
  if (!f || !name) return NULL;
  for (i = 0; i < f->n_entries; i++) {
    if (f->entries[i].name && strcmp(f->entries[i].name, name) == 0) return &f->entries[i];
  }
  return NULL;
}

const NeResource *ne_find_resource(const NeFile *f, uint16_t type, uint16_t id) {
  size_t i;
  if (!f) return NULL;
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    if (!r->type_name && !r->name && r->type == type && r->id == id) return r;
  }
  return NULL;
}

const NeResource *ne_find_resource_named(const NeFile *f, uint16_t type, const char *name) {
  size_t i;
  if (!f || !name) return NULL;
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    if (!r->type_name && r->name && r->type == type && strcmp(r->name, name) == 0) return r;
  }
  return NULL;
}

/* ---- DIALOG templates ---- */

/* Reads a menu / class / caption string-or-ordinal at data[*p]. Advances *p.
 * Ordinals are stored as "#N" in a malloc'd string; empty strings as NULL. */
static int read_str_or_ord(const uint8_t *d, size_t len, size_t *p, char **out) {
  *out = NULL;
  if (!in_range(len, *p, 1)) return NE_ERR_TRUNC;
  if (d[*p] == 0xFF) {
    char tmp[16];
    if (!in_range(len, *p, 3)) return NE_ERR_TRUNC;
    snprintf(tmp, sizeof tmp, "#%u", (unsigned)rd16(d + *p + 1));
    *p += 3;
    *out = dup_bytes((const uint8_t *)tmp, strlen(tmp));
    return *out ? NE_OK : NE_ERR_NOMEM;
  }
  {
    size_t start = *p, n = 0;
    while (in_range(len, start + n, 1) && d[start + n] != 0) n++;
    if (!in_range(len, start + n, 1)) return NE_ERR_TRUNC; /* unterminated */
    *p = start + n + 1;
    if (n == 0) return NE_OK;
    *out = dup_bytes(d + start, n);
    return *out ? NE_OK : NE_ERR_NOMEM;
  }
}

static int read_zstr(const uint8_t *d, size_t len, size_t *p, char **out) {
  size_t start = *p, n = 0;
  *out = NULL;
  while (in_range(len, start + n, 1) && d[start + n] != 0) n++;
  if (!in_range(len, start + n, 1)) return NE_ERR_TRUNC;
  *p = start + n + 1;
  *out = dup_bytes(d + start, n);
  return *out ? NE_OK : NE_ERR_NOMEM;
}

void ne_dialog_free(NeDialog *d) {
  size_t i;
  if (!d) return;
  for (i = 0; i < d->n_items; i++) {
    free(d->items[i].cls);
    free(d->items[i].text);
  }
  free(d->items);
  free(d->menu);
  free(d->cls);
  free(d->caption);
  free(d->font_name);
  memset(d, 0, sizeof *d);
}

int ne_parse_dialog(const uint8_t *data, size_t len, NeDialog *out) {
  size_t p = 0;
  unsigned i;
  int rc;
  if (!out) return NE_ERR_ARG;
  memset(out, 0, sizeof *out);
  if (!data) return NE_ERR_ARG;
  if (!in_range(len, 0, 13)) return NE_ERR_TRUNC;

  out->style = rd32(data);
  out->item_count = data[4];
  out->x = (int16_t)rd16(data + 5);
  out->y = (int16_t)rd16(data + 7);
  out->cx = (int16_t)rd16(data + 9);
  out->cy = (int16_t)rd16(data + 11);
  p = 13;

  rc = read_str_or_ord(data, len, &p, &out->menu);
  if (rc != NE_OK) goto fail;
  rc = read_str_or_ord(data, len, &p, &out->cls);
  if (rc != NE_OK) goto fail;
  rc = read_str_or_ord(data, len, &p, &out->caption);
  if (rc != NE_OK) goto fail;

  if (out->style & 0x40) {
    out->has_font = 1;
    if (!in_range(len, p, 2)) {
      rc = NE_ERR_TRUNC;
      goto fail;
    }
    out->font_size = rd16(data + p);
    p += 2;
    rc = read_zstr(data, len, &p, &out->font_name);
    if (rc != NE_OK) goto fail;
  }

  if (out->item_count) {
    out->items = calloc(out->item_count, sizeof(NeDialogItem));
    if (!out->items) {
      rc = NE_ERR_NOMEM;
      goto fail;
    }
    out->n_items = out->item_count; /* so ne_dialog_free sees every slot on failure */
  }
  for (i = 0; i < out->item_count; i++) {
    NeDialogItem *it = &out->items[i];
    uint8_t extra;
    if (!in_range(len, p, 14)) {
      rc = NE_ERR_TRUNC;
      goto fail;
    }
    it->x = (int16_t)rd16(data + p);
    it->y = (int16_t)rd16(data + p + 2);
    it->cx = (int16_t)rd16(data + p + 4);
    it->cy = (int16_t)rd16(data + p + 6);
    it->id = rd16(data + p + 8);
    it->style = rd32(data + p + 10);
    p += 14;
    /* Predefined class: one byte 0x80..0x85 (BUTTON, EDIT, STATIC, ...). */
    if (!in_range(len, p, 1)) {
      rc = NE_ERR_TRUNC;
      goto fail;
    }
    if (data[p] >= 0x80 && data[p] <= 0x85) {
      char tmp[8];
      snprintf(tmp, sizeof tmp, "#%u", (unsigned)data[p]);
      it->cls = dup_bytes((const uint8_t *)tmp, strlen(tmp));
      if (!it->cls) {
        rc = NE_ERR_NOMEM;
        goto fail;
      }
      p += 1;
    } else {
      rc = read_str_or_ord(data, len, &p, &it->cls);
      if (rc != NE_OK) goto fail;
    }
    rc = read_str_or_ord(data, len, &p, &it->text);
    if (rc != NE_OK) goto fail;
    if (!in_range(len, p, 1)) {
      rc = NE_ERR_TRUNC;
      goto fail;
    }
    extra = data[p++];
    if (!in_range(len, p, extra)) {
      rc = NE_ERR_TRUNC;
      goto fail;
    }
    it->extra_bytes = extra;
    p += extra;
  }
  return NE_OK;

fail:
  ne_dialog_free(out);
  memset(out, 0, sizeof *out);
  return rc;
}
