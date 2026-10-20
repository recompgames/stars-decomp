/* nedump: prints and dumps the contents of a Win16 NE image (.exe, .fon, .dll).
 *
 *   nedump <file>                       summary
 *   nedump --segments --relocs --imports --exports --resources <file>
 *   nedump --dump-resources <dir> <file>
 *
 * Dump mode writes every resource raw (.bin). BITMAP resources are also written as .bmp,
 * DIALOG and MENU resources as decoded text (.txt). Parsing is done by runtime/ne.c.
 */
#include "runtime/ne.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MF_POPUP 0x0010
#define MF_END 0x0080

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *fp = fopen(path, "rb");
  long n;
  uint8_t *buf;
  if (!fp) {
    fprintf(stderr, "nedump: cannot open %s: %s\n", path, strerror(errno));
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fprintf(stderr, "nedump: cannot size %s\n", path);
    fclose(fp);
    return NULL;
  }
  buf = malloc((size_t)n ? (size_t)n : 1);
  if (!buf || (n && fread(buf, 1, (size_t)n, fp) != (size_t)n)) {
    fprintf(stderr, "nedump: cannot read %s\n", path);
    free(buf);
    fclose(fp);
    return NULL;
  }
  fclose(fp);
  *len = (size_t)n;
  return buf;
}

static const char *module_or_q(const NeFile *f, uint16_t m) {
  const char *s = ne_module_name(f, m);
  return s ? s : "?";
}

static const char *src_name(uint8_t src) {
  switch (src) {
    case NE_SRC_LOBYTE: return "LOBYTE";
    case NE_SRC_SEGMENT: return "SEGMENT";
    case NE_SRC_FAR_ADDR: return "FAR_ADDR";
    case NE_SRC_OFFSET: return "OFFSET";
    default: return "?";
  }
}

static void print_summary(const NeFile *f) {
  size_t i, n_int = 0, n_ord = 0, n_name = 0, n_os = 0;
  for (i = 0; i < f->n_relocs; i++) {
    switch (f->relocs[i].kind) {
      case NE_FIX_INTERNAL: n_int++; break;
      case NE_FIX_ORDINAL: n_ord++; break;
      case NE_FIX_NAME: n_name++; break;
      case NE_FIX_OSFIXUP: n_os++; break;
      default: break;
    }
  }
  printf("module       %s\n", f->module_name ? f->module_name : "");
  printf("description  %s\n", f->description ? f->description : "");
  printf("linker       %u.%u\n", f->hdr.linker_major, f->hdr.linker_minor);
  printf("flags        0x%04x\n", f->hdr.flags);
  printf("entry        %u:0x%04x\n", f->hdr.entry_cs, f->hdr.entry_ip);
  printf("auto data    %u\n", f->hdr.auto_data_seg);
  printf("heap/stack   %u / %u\n", f->hdr.heap_size, f->hdr.stack_size);
  printf("align shift  %u\n", f->hdr.align_shift);
  printf("segments     %zu\n", f->n_segs);
  printf("modules      %zu\n", f->n_modules);
  printf("relocs       %zu (internal %zu, ordinal %zu, name %zu, osfixup %zu)\n", f->n_relocs,
         n_int, n_ord, n_name, n_os);
  printf("sites        %zu\n", f->n_sites);
  printf("exports      %zu\n", f->n_entries);
  printf("resources    %zu\n", f->n_resources);
}

static void print_segments(const NeFile *f) {
  size_t i;
  printf("segments:\n");
  for (i = 0; i < f->n_segs; i++) {
    const NeSegment *s = &f->segs[i];
    printf("  %3zu %-5s file=0x%06x length=%6u min_alloc=%6u flags=0x%04x%s\n", i + 1,
           (s->flags & 1) ? "DATA" : "CODE", s->file_off, s->length, s->min_alloc, s->flags,
           (s->flags & 0x0100) ? " relocs" : "");
  }
}

static void print_relocs(const NeFile *f) {
  size_t i;
  static const char *kinds[4] = {"internal", "ordinal", "name", "osfixup"};
  printf("relocation records:\n");
  for (i = 0; i < f->n_relocs; i++) {
    const NeFixup *r = &f->relocs[i];
    printf("  %3u:0x%04x %-8s %-8s %s w1=0x%04x w2=0x%04x\n", r->seg, r->site, src_name(r->src),
           kinds[r->kind & 3], r->additive ? "additive" : "chain", r->w1, r->w2);
  }
}

static void print_imports(const NeFile *f) {
  size_t i;
  printf("import sites:\n");
  for (i = 0; i < f->n_sites; i++) {
    const NeFixup *s = &f->sites[i];
    char name[256];
    if (s->kind == NE_FIX_ORDINAL) {
      printf("  %3u:0x%04x %s.%u\n", s->seg, s->site, module_or_q(f, s->w1), s->w2);
    } else if (s->kind == NE_FIX_NAME) {
      if (ne_import_name(f, s->w2, name, sizeof name) != NE_OK) strcpy(name, "?");
      printf("  %3u:0x%04x %s.%s\n", s->seg, s->site, module_or_q(f, s->w1), name);
    }
  }
}

static void print_exports(const NeFile *f) {
  size_t i;
  printf("entries:\n");
  for (i = 0; i < f->n_entries; i++) {
    const NeExport *e = &f->entries[i];
    if (e->seg == 0) continue;
    printf("  %5u %3u:0x%04x %s\n", e->ordinal, e->seg, e->off, e->name ? e->name : "");
  }
}

static const char *type_label(const NeResource *r, char *buf, size_t cap) {
  static const char *known[] = {"",       "cursor", "bitmap",  "icon",     "menu",
                                "dialog", "string", "fontdir", "font",     "accelerator",
                                "rcdata"};
  if (r->type_name) {
    snprintf(buf, cap, "%s", r->type_name);
  } else if (r->type <= 10) {
    snprintf(buf, cap, "%s", known[r->type]);
  } else if (r->type == 12) {
    snprintf(buf, cap, "group_cursor");
  } else if (r->type == 14) {
    snprintf(buf, cap, "group_icon");
  } else {
    snprintf(buf, cap, "type%u", (unsigned)r->type);
  }
  return buf;
}

static void print_resources(const NeFile *f) {
  size_t i;
  printf("resources:\n");
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    char label[64];
    type_label(r, label, sizeof label);
    if (r->name) {
      printf("  %-14s name=%-16s offset=0x%06x size=%u\n", label, r->name, r->offset, r->size);
    } else {
      printf("  %-14s id=%-16u offset=0x%06x size=%u\n", label, (unsigned)r->id, r->offset,
             r->size);
    }
  }
}

/* Makes a file-name component from a resource name or number. */
static void safe_part(const char *s, char *out, size_t cap) {
  size_t i = 0;
  for (; s[i] && i + 1 < cap; i++) {
    unsigned char c = (unsigned char)s[i];
    out[i] = (c < 128 && (c == '_' || c == '-' || (c >= '0' && c <= '9') ||
                          (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')))
                 ? (char)c
                 : '_';
  }
  out[i] = '\0';
}

static int write_file(const char *path, const void *data, size_t n) {
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    fprintf(stderr, "nedump: cannot write %s: %s\n", path, strerror(errno));
    return -1;
  }
  if (n && fwrite(data, 1, n, fp) != n) {
    fprintf(stderr, "nedump: short write to %s\n", path);
    fclose(fp);
    return -1;
  }
  return fclose(fp) == 0 ? 0 : -1;
}

/* Prepends a BITMAPFILEHEADER to a DIB (BITMAPINFOHEADER, palette, bits). */
static int write_bmp(const char *path, const uint8_t *dib, size_t n) {
  uint8_t hdr[14];
  uint32_t bi_size, clr_used, off_bits, colors, entry;
  uint16_t bpp;
  size_t total;
  if (n < 16) return -1;
  bi_size = rd32(dib);
  if (bi_size < 12 || bi_size > n) return -1;
  bpp = rd16(dib + 14);
  clr_used = n >= 36 ? rd32(dib + 32) : 0;
  colors = clr_used ? clr_used : (bpp <= 8 ? (1u << bpp) : 0);
  entry = bi_size == 12 ? 3 : 4;
  off_bits = 14 + bi_size + colors * entry;
  if (off_bits > 14 + n) return -1;
  total = 14 + n;
  memset(hdr, 0, sizeof hdr);
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = (uint8_t)total;
  hdr[3] = (uint8_t)(total >> 8);
  hdr[4] = (uint8_t)(total >> 16);
  hdr[5] = (uint8_t)(total >> 24);
  hdr[10] = (uint8_t)off_bits;
  hdr[11] = (uint8_t)(off_bits >> 8);
  hdr[12] = (uint8_t)(off_bits >> 16);
  hdr[13] = (uint8_t)(off_bits >> 24);
  {
    FILE *fp = fopen(path, "wb");
    if (!fp) {
      fprintf(stderr, "nedump: cannot write %s: %s\n", path, strerror(errno));
      return -1;
    }
    if (fwrite(hdr, 1, 14, fp) != 14 || fwrite(dib, 1, n, fp) != n) {
      fclose(fp);
      return -1;
    }
    return fclose(fp) == 0 ? 0 : -1;
  }
}

static const char *predef_class(const char *cls) {
  static const char *names[] = {"BUTTON", "EDIT", "STATIC", "LISTBOX", "SCROLLBAR", "COMBOBOX"};
  if (cls && cls[0] == '#') {
    unsigned v = (unsigned)strtoul(cls + 1, NULL, 10);
    if (v >= 0x80 && v <= 0x85) return names[v - 0x80];
  }
  return NULL;
}

static void print_str(FILE *out, const char *s) {
  if (!s) {
    fputs("-", out);
    return;
  }
  if (s[0] == '#') {
    fputs(s, out);
    return;
  }
  fputc('"', out);
  fputs(s, out);
  fputc('"', out);
}

static int dialog_text(FILE *out, const uint8_t *data, size_t len) {
  NeDialog d;
  size_t i;
  int rc = ne_parse_dialog(data, len, &d);
  if (rc != NE_OK) return rc;
  fprintf(out, "DIALOG style=0x%08lx pos=%d,%d size=%dx%d items=%u\n", (unsigned long)d.style, d.x,
          d.y, d.cx, d.cy, (unsigned)d.item_count);
  fputs("  menu=", out);
  print_str(out, d.menu);
  fputs(" class=", out);
  print_str(out, d.cls);
  fputs(" caption=", out);
  print_str(out, d.caption);
  fputc('\n', out);
  if (d.has_font) {
    fputs("  font=", out);
    print_str(out, d.font_name);
    fprintf(out, " %upt\n", (unsigned)d.font_size);
  }
  for (i = 0; i < d.n_items; i++) {
    const NeDialogItem *it = &d.items[i];
    const char *pc = predef_class(it->cls);
    fprintf(out, "  [%zu] id=%u pos=%d,%d size=%dx%d style=0x%08lx class=", i, it->id, it->x, it->y,
            it->cx, it->cy, (unsigned long)it->style);
    if (pc) {
      fputs(pc, out);
    } else {
      print_str(out, it->cls);
    }
    fputs(" text=", out);
    print_str(out, it->text);
    fputc('\n', out);
  }
  ne_dialog_free(&d);
  return 0;
}

/* Walks one menu level: flags, then an id unless POPUP, then an ASCIIZ string. */
static int menu_level(FILE *out, const uint8_t *d, size_t len, size_t *p, int depth) {
  for (;;) {
    uint16_t flags;
    int popup;
    size_t start, n = 0;
    if (*p + 2 > len) return NE_ERR_TRUNC;
    flags = rd16(d + *p);
    *p += 2;
    popup = (flags & MF_POPUP) != 0;
    if (!popup) {
      if (*p + 2 > len) return NE_ERR_TRUNC;
      *p += 2;
    }
    start = *p;
    while (*p + n < len && d[*p + n] != 0) n++;
    if (*p + n >= len) return NE_ERR_TRUNC;
    fprintf(out, "%*s", depth * 2, "");
    fprintf(out, "%s", popup ? "POPUP " : "ITEM ");
    if (!popup) {
      fprintf(out, "id=%u ", (unsigned)rd16(d + *p - 2));
    }
    fprintf(out, "\"%.*s\"", (int)n, (const char *)(d + start));
    fprintf(out, " flags=0x%04x\n", flags);
    *p = start + n + 1;
    if (popup) {
      int rc = menu_level(out, d, len, p, depth + 1);
      if (rc != NE_OK) return rc;
    }
    if (flags & MF_END) return NE_OK;
  }
}

static int menu_text(FILE *out, const uint8_t *data, size_t len) {
  size_t p;
  if (len < 4) return NE_ERR_TRUNC;
  p = rd16(data + 2) ? rd16(data + 2) : 4; /* header: version, offset of first item */
  if (p > len) return NE_ERR_TRUNC;
  fprintf(out, "MENU version=%u\n", (unsigned)rd16(data));
  return menu_level(out, data, len, &p, 1);
}

static int dump_resources(const char *dir, const NeFile *f, const uint8_t *buf, size_t len) {
  size_t i;
  char path[4096], label[64], part[256];
  if (mkdir(dir, 0777) != 0 && errno != EEXIST) {
    /* Create missing parents too (re/resources needs re/ first). */
    char tmp[4096];
    char *slash;
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (slash = tmp + 1; *slash; slash++) {
      if (*slash == '/') {
        *slash = '\0';
        mkdir(tmp, 0777);
        *slash = '/';
      }
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
      fprintf(stderr, "nedump: cannot create %s: %s\n", dir, strerror(errno));
      return -1;
    }
  }
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    const uint8_t *data;
    FILE *txt;
    int rc = 0;
    type_label(r, label, sizeof label);
    if (r->name) {
      safe_part(r->name, part, sizeof part);
    } else {
      snprintf(part, sizeof part, "%u", (unsigned)r->id);
    }
    if (r->offset > len || r->size > len - r->offset) {
      fprintf(stderr, "nedump: resource %zu lies outside the file\n", i);
      return -1;
    }
    data = buf + r->offset;
    snprintf(path, sizeof path, "%s/%04zu-%s-%s.bin", dir, i, label, part);
    if (write_file(path, data, r->size) != 0) return -1;

    if (!r->type_name && r->type == 2) {
      snprintf(path, sizeof path, "%s/%04zu-%s-%s.bmp", dir, i, label, part);
      if (write_bmp(path, data, r->size) != 0) {
        fprintf(stderr, "nedump: bitmap %zu is not a DIB\n", i);
        return -1;
      }
    } else if (!r->type_name && (r->type == 5 || r->type == 4)) {
      snprintf(path, sizeof path, "%s/%04zu-%s-%s.txt", dir, i, label, part);
      txt = fopen(path, "w");
      if (!txt) {
        fprintf(stderr, "nedump: cannot write %s: %s\n", path, strerror(errno));
        return -1;
      }
      rc = r->type == 5 ? dialog_text(txt, data, r->size) : menu_text(txt, data, r->size);
      if (fclose(txt) != 0 || rc != NE_OK) {
        fprintf(stderr, "nedump: cannot decode resource %zu (%s)\n", i, label);
        return -1;
      }
    }
  }
  printf("wrote %zu resources to %s\n", f->n_resources, dir);
  return 0;
}

static void usage(void) {
  fprintf(stderr,
          "usage: nedump [--segments] [--relocs] [--imports] [--exports] [--resources] <file>\n"
          "       nedump --dump-resources <dir> <file>\n");
}

int main(int argc, char **argv) {
  int want_segments = 0, want_relocs = 0, want_imports = 0, want_exports = 0, want_resources = 0;
  const char *dump_dir = NULL, *path = NULL;
  int i, any_section = 0, rc = 1;
  size_t len = 0;
  uint8_t *buf;
  NeFile f;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "--segments")) want_segments = 1;
    else if (!strcmp(a, "--relocs")) want_relocs = 1;
    else if (!strcmp(a, "--imports")) want_imports = 1;
    else if (!strcmp(a, "--exports")) want_exports = 1;
    else if (!strcmp(a, "--resources")) want_resources = 1;
    else if (!strcmp(a, "--dump-resources")) {
      if (i + 1 >= argc) {
        usage();
        return 2;
      }
      dump_dir = argv[++i];
    } else if (a[0] == '-') {
      usage();
      return 2;
    } else {
      path = a;
    }
  }
  if (!path) {
    usage();
    return 2;
  }

  buf = read_file(path, &len);
  if (!buf) return 1;
  memset(&f, 0, sizeof f);
  if (ne_parse(buf, len, &f) != NE_OK) {
    fprintf(stderr, "nedump: %s is not a valid NE image\n", path);
    goto out;
  }

  if (dump_dir) {
    rc = dump_resources(dump_dir, &f, buf, len) == 0 ? 0 : 1;
    goto out;
  }

  any_section = want_segments || want_relocs || want_imports || want_exports || want_resources;
  if (!any_section) print_summary(&f);
  if (want_segments) print_segments(&f);
  if (want_relocs) print_relocs(&f);
  if (want_imports) print_imports(&f);
  if (want_exports) print_exports(&f);
  if (want_resources) print_resources(&f);
  rc = 0;

out:
  ne_free(&f);
  free(buf);
  return rc;
}
