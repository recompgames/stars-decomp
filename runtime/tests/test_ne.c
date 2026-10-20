/* Expectations for the NE loader (runtime/ne.c) against original/stars26i.exe.
 * Facts measured 2026-10-15 (issue stars-57q.3). Do not edit an expectation to
 * match the code: a disagreement is reported, not absorbed.
 */
#include "test.h"
#include "runtime/ne.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXE_PATH "original/stars26i.exe"

/* Resource type IDs (Win16 RT_* numbering). */
#define RT_CURSOR 1
#define RT_BITMAP 2
#define RT_ICON 3
#define RT_MENU 4
#define RT_DIALOG 5
#define RT_ACCELERATOR 9
#define RT_GROUP_CURSOR 12
#define RT_GROUP_ICON 14

static uint8_t *load_exe(size_t *len) {
  FILE *fp = fopen(EXE_PATH, "rb");
  uint8_t *buf;
  long n;
  if (!fp) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  buf = malloc((size_t)n);
  if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) {
    free(buf);
    fclose(fp);
    return NULL;
  }
  fclose(fp);
  *len = (size_t)n;
  return buf;
}

/* First site of the given kind at seg:off, or NULL. */
static const NeFixup *find_site(const NeFile *f, uint8_t kind, uint16_t seg, uint16_t off) {
  size_t i;
  for (i = 0; i < f->n_sites; i++) {
    const NeFixup *s = &f->sites[i];
    if (s->kind == kind && s->seg == seg && s->site == off) {
      return s;
    }
  }
  return NULL;
}

static size_t count_imports(const NeFile *f, const char *module, uint16_t ordinal) {
  size_t i, n = 0;
  for (i = 0; i < f->n_sites; i++) {
    const NeFixup *s = &f->sites[i];
    if (s->kind == NE_FIX_ORDINAL && s->w2 == ordinal) {
      const char *m = ne_module_name(f, s->w1);
      if (m && strcmp(m, module) == 0) {
        n++;
      }
    }
  }
  return n;
}

static void test_header(const NeFile *f) {
  CHECK_EQ(f->hdr.linker_major, 5);
  CHECK_EQ(f->hdr.linker_minor, 60);
  CHECK_EQ(f->hdr.flags, 0x0302);
  CHECK_EQ(f->hdr.seg_count, 35);
  CHECK_EQ(f->n_segs, 35);
  CHECK_EQ(f->hdr.entry_cs, 34);
  CHECK_EQ(f->hdr.entry_ip, 0x001A);
  CHECK_EQ(f->hdr.auto_data_seg, 35);
  CHECK_EQ(f->hdr.heap_size, 8192);
  CHECK_EQ(f->hdr.stack_size, 16384);
  CHECK_EQ(f->hdr.align_shift, 6);

  CHECK_EQ(f->hdr.modref_count, 6);
  CHECK_EQ(f->n_modules, 6);
  {
    static const char *want[6] = {"WIN87EM", "KERNEL", "GDI", "USER", "COMMDLG", "TOOLHELP"};
    size_t i;
    for (i = 0; i < 6 && i < f->n_modules; i++) {
      const char *got = ne_module_name(f, (uint16_t)(i + 1));
      CHECK(got != NULL && strcmp(got, want[i]) == 0);
    }
  }
}

static void test_names(const NeFile *f) {
  CHECK(f->module_name != NULL && strcmp(f->module_name, "STARS!") == 0);
  CHECK(f->description != NULL &&
        strcmp(f->description, "Stars! Intergalactic Conquest Game") == 0);
}

static void test_segments(const NeFile *f) {
  uint32_t sum = 0;
  size_t i;
  CHECK_EQ(f->segs[0].file_off, 0x1140);
  CHECK_EQ(f->segs[0].length, 7938);
  CHECK_EQ(f->segs[0].flags & 1, 0); /* CODE */
  CHECK_EQ(f->segs[33].file_off, 0x50DC0);
  CHECK_EQ(f->segs[33].length, 14391);
  CHECK_EQ(f->segs[33].flags & 1, 0); /* CODE */
  CHECK_EQ(f->segs[34].file_off, 0x54E40);
  CHECK_EQ(f->segs[34].length, 26248);
  CHECK_EQ(f->segs[34].flags & 1, 1); /* DATA */
  for (i = 0; i < f->n_segs; i++) {
    sum += f->segs[i].length;
  }
  CHECK_EQ(sum, 715684);
}

static void test_relocs(const NeFile *f) {
  size_t i;
  size_t n_int = 0, n_ord = 0, n_name = 0, n_os = 0;
  size_t n_int_src2_nonadd = 0, n_ord_src3 = 0, n_ord_src5 = 0;
  size_t n_os_src5_add = 0;
  size_t hist[8] = {0};

  CHECK_EQ(f->n_relocs, 2228);
  for (i = 0; i < f->n_relocs; i++) {
    const NeFixup *r = &f->relocs[i];
    switch (r->kind) {
      case NE_FIX_INTERNAL:
        n_int++;
        if (r->src == NE_SRC_SEGMENT && !r->additive) n_int_src2_nonadd++;
        break;
      case NE_FIX_ORDINAL:
        n_ord++;
        if (r->src == NE_SRC_FAR_ADDR) n_ord_src3++;
        if (r->src == NE_SRC_OFFSET) n_ord_src5++;
        break;
      case NE_FIX_NAME:
        n_name++;
        break;
      case NE_FIX_OSFIXUP:
        n_os++;
        if (r->src == NE_SRC_OFFSET && r->additive) n_os_src5_add++;
        if (r->w1 < 8) hist[r->w1]++;
        break;
      default:
        break;
    }
  }
  CHECK_EQ(n_int, 390);
  CHECK_EQ(n_int_src2_nonadd, 390);
  CHECK_EQ(n_ord, 1028);
  CHECK_EQ(n_ord_src3, 1024);
  CHECK_EQ(n_ord_src5, 4);
  CHECK_EQ(n_name, 0);
  CHECK_EQ(n_os, 810);
  CHECK_EQ(n_os_src5_add, 810);

  /* OSFIXUP fix-type histogram, first word of the record. */
  CHECK_EQ(hist[2], 1);
  CHECK_EQ(hist[3], 4);
  CHECK_EQ(hist[4], 14);
  CHECK_EQ(hist[5], 737);
  CHECK_EQ(hist[6], 54);
  CHECK_EQ(hist[0] + hist[1] + hist[7], 0);
}

static void test_expanded_sites(const NeFile *f) {
  size_t i;
  size_t n_int = 0, n_imp = 0, n_os = 0;
  for (i = 0; i < f->n_sites; i++) {
    switch (f->sites[i].kind) {
      case NE_FIX_INTERNAL: n_int++; break;
      case NE_FIX_ORDINAL:
      case NE_FIX_NAME: n_imp++; break;
      case NE_FIX_OSFIXUP: n_os++; break;
      default: break;
    }
  }
  CHECK_EQ(n_int, 5203);
  CHECK_EQ(n_imp, 5165);
  CHECK_EQ(n_os, 810);
}

static void test_osfixup_sites(const uint8_t *buf, const NeFile *f) {
  const NeFixup *s;
  s = find_site(f, NE_FIX_OSFIXUP, 5, 0x1586);
  CHECK(s != NULL);
  CHECK_MEM(buf + f->segs[4].file_off + 0x1586, "\x9B\x26\xDB\x47", 4);

  s = find_site(f, NE_FIX_OSFIXUP, 34, 0x14F1);
  CHECK(s != NULL);
  CHECK_MEM(buf + f->segs[33].file_off + 0x14F1, "\x9B\x36\xDB\x3F", 4);

  s = find_site(f, NE_FIX_OSFIXUP, 7, 0x033B);
  CHECK(s != NULL);
  CHECK_MEM(buf + f->segs[6].file_off + 0x033B, "\x90\x9B\x8B\x46", 4);
}

static void test_import_sites(const uint8_t *buf, const NeFile *f) {
  const NeFixup *s = find_site(f, NE_FIX_ORDINAL, 34, 0x001E);
  CHECK(s != NULL);
  if (s) {
    const char *m = ne_module_name(f, s->w1);
    CHECK(m != NULL && strcmp(m, "KERNEL") == 0);
    CHECK_EQ(s->w2, 91);
  }
  CHECK_MEM(buf + f->segs[33].file_off + 0x001A,
            "\x33\xED\x55\x9A\xFF\xFF\x00\x00", 8);

  CHECK_EQ(count_imports(f, "KERNEL", 102), 21);
  CHECK_EQ(count_imports(f, "KERNEL", 114), 7);
  CHECK_EQ(count_imports(f, "KERNEL", 113), 2);
  CHECK_EQ(count_imports(f, "KERNEL", 178), 1);
}

static void test_segment35(const NeFile *f) {
  const NeFixup *s;
  s = find_site(f, NE_FIX_INTERNAL, 35, 0x1E92);
  CHECK(s != NULL && s->w1 == 34);
  s = find_site(f, NE_FIX_INTERNAL, 35, 0x08DE);
  CHECK(s != NULL && s->w1 == 9);
  s = find_site(f, NE_FIX_INTERNAL, 35, 0x08EE);
  CHECK(s != NULL && s->w1 == 10);

  s = find_site(f, NE_FIX_ORDINAL, 35, 0x1E7C);
  CHECK(s != NULL);
  if (s) {
    const char *m = ne_module_name(f, s->w1);
    CHECK(m != NULL && strcmp(m, "WIN87EM") == 0);
    CHECK_EQ(s->w2, 1);
    CHECK_EQ(s->src, NE_SRC_FAR_ADDR);
  }
}

static void check_export(const NeFile *f, const char *name, uint8_t seg, uint16_t off) {
  const NeExport *e = ne_find_export(f, name);
  CHECK(e != NULL);
  if (e) {
    CHECK_EQ(e->seg, seg);
    CHECK_EQ(e->off, off);
  }
}

static void test_entries(const NeFile *f) {
  CHECK_EQ(f->n_entries, 53);
  check_export(f, "PLANETWNDPROC", 9, 0x0000);
  check_export(f, "RACEWIZARDDLG1", 28, 0x0336);
  check_export(f, "___EXPORTEDSTUB", 34, 0x2726);
  check_export(f, "FRAMEWNDPROC", 4, 0x0528);
}

static size_t count_type(const NeFile *f, uint16_t type) {
  size_t i, n = 0;
  for (i = 0; i < f->n_resources; i++) {
    if (!f->resources[i].type_name && f->resources[i].type == type) n++;
  }
  return n;
}

static size_t sum_size(const NeFile *f, uint16_t type) {
  size_t i, n = 0;
  for (i = 0; i < f->n_resources; i++) {
    if (!f->resources[i].type_name && f->resources[i].type == type) n += f->resources[i].size;
  }
  return n;
}

/* Checks that the resources of an integer type have exactly the given IDs, in table order. */
static void check_ids(const NeFile *f, uint16_t type, const uint16_t *ids, size_t n) {
  size_t i, k = 0;
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    if (!r->type_name && r->type == type) {
      if (k < n) {
        CHECK_EQ(r->id, ids[k]);
      }
      k++;
    }
  }
  CHECK_EQ(k, n);
}

static void test_resources(const NeFile *f) {
  static const uint16_t cursor_ids[11] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
  static const uint16_t icon_ids[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  static const uint16_t accel_ids[2] = {116, 1080};
  const NeResource *r;

  CHECK_EQ(count_type(f, RT_GROUP_CURSOR), 11);
  CHECK_EQ(count_type(f, RT_GROUP_ICON), 10);
  CHECK_EQ(count_type(f, RT_BITMAP), 38);
  CHECK_EQ(sum_size(f, RT_BITMAP), 2376000);
  CHECK_EQ(count_type(f, RT_MENU), 1);
  CHECK_EQ(count_type(f, RT_DIALOG), 36);
  CHECK_EQ(count_type(f, RT_ACCELERATOR), 2);
  CHECK_EQ(count_type(f, RT_ICON), 10);
  CHECK_EQ(count_type(f, RT_CURSOR), 11);
  check_ids(f, RT_ACCELERATOR, accel_ids, 2);
  check_ids(f, RT_ICON, icon_ids, 10);
  check_ids(f, RT_CURSOR, cursor_ids, 11);

  r = ne_find_resource_named(f, RT_MENU, "STARSMENU");
  CHECK(r != NULL);

  r = ne_find_resource(f, 10000, 10001);
  CHECK(r != NULL && r->size == 4352);
  r = ne_find_resource(f, 10002, 10003);
  CHECK(r != NULL && r->size == 51776);
  r = ne_find_resource(f, 10004, 10005);
  CHECK(r != NULL && r->size == 19904);
}

static void test_dialogs(const uint8_t *buf, size_t len, const NeFile *f) {
  static const uint16_t ids[36] = {
      90,   97,   1068, 2025, 91,  214, 209, 390, 391, 392, 160,  115,
      1026, 92,   146,  147,  148, 149, 150, 151, 93,  82,  2013, 2008,
      127,  89,   393,  128,  2502, 2504, 140, 141, 2019, 86,  4202, 102};
  size_t i;

  check_ids(f, RT_DIALOG, ids, 36);
  for (i = 0; i < f->n_resources; i++) {
    const NeResource *r = &f->resources[i];
    NeDialog d;
    if (r->type_name || r->type != RT_DIALOG) continue;
    CHECK(r->offset <= len && r->size <= len - r->offset);
    if (r->offset > len || r->size > len - r->offset) continue;
    memset(&d, 0, sizeof d);
    CHECK_EQ(ne_parse_dialog(buf + r->offset, r->size, &d), NE_OK);
    CHECK(d.style & 0x40); /* DS_SETFONT */
    CHECK(d.has_font);
    CHECK(d.font_name != NULL && strcmp(d.font_name, "MS Sans Serif") == 0);
    CHECK_EQ(d.font_size, 8);
    ne_dialog_free(&d);
  }
}

static void test_bitmap(const uint8_t *buf, size_t len, const NeFile *f) {
  const NeResource *r = ne_find_resource_named(f, RT_BITMAP, "CARGOBMP");
  const uint8_t *p;
  CHECK(r != NULL);
  if (!r) return;
  CHECK(r->offset <= len && r->size >= 40 && r->size <= len - r->offset);
  if (r->offset > len || r->size < 40 || r->size > len - r->offset) return;
  p = buf + r->offset;
  CHECK_EQ(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24), 40);      /* biSize */
  CHECK_EQ(p[4] | (p[5] << 8) | (p[6] << 16) | ((uint32_t)p[7] << 24), 8);       /* biWidth */
  CHECK_EQ(p[8] | (p[9] << 8) | (p[10] << 16) | ((uint32_t)p[11] << 24), 8);     /* biHeight */
  CHECK_EQ(p[12] | (p[13] << 8), 1);                                             /* biPlanes */
  CHECK_EQ(p[14] | (p[15] << 8), 4);                                             /* biBitCount */
}

static void test_truncated(const uint8_t *buf) {
  uint8_t *cut = malloc(0x2000);
  NeFile g;
  int rc;
  CHECK(cut != NULL);
  if (!cut) return;
  memcpy(cut, buf, 0x2000);
  memset(&g, 0, sizeof g);
  rc = ne_parse(cut, 0x2000, &g);
  CHECK(rc != NE_OK);
  ne_free(&g); /* must be safe after a failed parse */
  free(cut);
}

static void test_bad_input(void) {
  NeFile g;
  uint8_t tiny[4] = {'M', 'Z', 0, 0};
  memset(&g, 0, sizeof g);
  CHECK(ne_parse(tiny, sizeof tiny, &g) != NE_OK);
  ne_free(&g);
}

/* Minimal synthetic NE image for the hostile-input tests: MZ stub, NE header at 0x40,
 * one segment of 16 bytes at file offset 0x300 (align shift 4) with the relocation flag. */
#define SYN_NE 0x40
#define SYN_SEG 0x300
#define SYN_SIZE 0x1000
static void syn_w16(uint8_t *b, size_t o, unsigned v) {
  b[o] = (uint8_t)v;
  b[o + 1] = (uint8_t)(v >> 8);
}
static void syn_build(uint8_t *b, unsigned entry_len) {
  uint8_t *h = b + SYN_NE;
  memset(b, 0, SYN_SIZE);
  b[0] = 'M';
  b[1] = 'Z';
  b[0x3C] = SYN_NE;
  h[0] = 'N';
  h[1] = 'E';
  syn_w16(h, 0x04, 0x4A);      /* entry table offset */
  syn_w16(h, 0x06, entry_len); /* entry table length */
  syn_w16(h, 0x1C, 1);         /* one segment */
  syn_w16(h, 0x22, 0x40);      /* segment table */
  syn_w16(h, 0x26, 0x48);      /* resident names (empty) */
  syn_w16(h, 0x28, 0x49);      /* module refs */
  syn_w16(h, 0x2A, 0x49);      /* imported names */
  syn_w16(h, 0x32, 4);         /* align shift */
  syn_w16(h, 0x40, SYN_SEG >> 4); /* segment 1: sector */
  syn_w16(h, 0x42, 16);           /* length */
  syn_w16(h, 0x44, 0x0100);       /* flags: has relocations */
  syn_w16(h, 0x46, 16);           /* min alloc */
}
/* Appends n additive records (source type 0, site 0) after segment 1. */
static void syn_additive_records(uint8_t *b, unsigned n) {
  unsigned i;
  syn_w16(b, SYN_SEG + 16, n);
  for (i = 0; i < n; i++) {
    uint8_t *r = b + SYN_SEG + 18 + i * 8;
    r[0] = 2;
    r[1] = 0x04 | NE_FIX_INTERNAL;
    syn_w16(r, 2, 0);
  }
}

static int zeroed(const NeFile *g) {
  NeFile z;
  memset(&z, 0, sizeof z);
  return memcmp(g, &z, sizeof z) == 0;
}

static void test_synthetic(void) {
  uint8_t *b = malloc(SYN_SIZE);
  NeFile g;
  unsigned i;
  CHECK(b != NULL);
  if (!b) return;

  /* Baseline: the synthetic image itself parses. */
  syn_build(b, 1);
  syn_additive_records(b, 8); /* exactly seg_len/2 sites */
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_SIZE, &g), NE_OK);
  CHECK_EQ(g.n_sites, 8);
  ne_free(&g);

  /* Per-segment cap: one site over seg_len/2 is rejected, with *out zeroed. */
  syn_build(b, 1);
  syn_additive_records(b, 9);
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_SIZE, &g), NE_ERR_FORMAT);
  CHECK(zeroed(&g));
  ne_free(&g);

  /* Many records, each a short chain: total still capped per segment. */
  syn_build(b, 1);
  syn_w16(b, SYN_SEG, 0xFFFF); /* site 0 ends its chain immediately */
  syn_w16(b, SYN_SEG + 16, 200);
  for (i = 0; i < 200; i++) {
    uint8_t *r = b + SYN_SEG + 18 + i * 8;
    r[0] = 2;
    r[1] = NE_FIX_INTERNAL; /* not additive: chain */
    syn_w16(r, 2, 0);       /* one site per record */
  }
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_SIZE, &g), NE_ERR_FORMAT);
  CHECK(zeroed(&g));
  ne_free(&g);

  /* Entry table: 65535 entries (ordinal range) is fine, 65536 is not. */
  syn_build(b, 257 * 2 + 1); /* 257 bundles of 255 unused = 65535 entries */
  for (i = 0; i < 257; i++) {
    b[SYN_NE + 0x4A + i * 2] = 255;
    b[SYN_NE + 0x4A + i * 2 + 1] = 0;
  }
  syn_w16(b, SYN_NE + 0x1C, 0); /* no segments: isolate the entry table */
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_SIZE, &g), NE_OK);
  CHECK_EQ(g.n_entries, 65535);
  ne_free(&g);

  syn_build(b, 257 * 2 + 3);
  for (i = 0; i < 257; i++) {
    b[SYN_NE + 0x4A + i * 2] = 255;
    b[SYN_NE + 0x4A + i * 2 + 1] = 0;
  }
  b[SYN_NE + 0x4A + 257 * 2] = 1; /* one more entry: 65536 */
  b[SYN_NE + 0x4A + 257 * 2 + 1] = 0;
  syn_w16(b, SYN_NE + 0x1C, 0);
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_SIZE, &g), NE_ERR_FORMAT);
  CHECK(zeroed(&g));
  ne_free(&g);

  /* A truncated segment table: error leaves *out fully zeroed (header was already read). */
  syn_build(b, 1);
  memset(&g, 0, sizeof g);
  CHECK_EQ(ne_parse(b, SYN_NE + 0x40 + 4, &g), NE_ERR_TRUNC);
  CHECK(zeroed(&g));
  ne_free(&g);

  free(b);
}

int main(void) {
  size_t len = 0;
  uint8_t *buf = load_exe(&len);
  NeFile f;
  int rc;

  if (!buf) {
    fprintf(stderr, "cannot read %s\n", EXE_PATH);
    CHECK(0);
    return TEST_DONE();
  }

  memset(&f, 0, sizeof f);
  rc = ne_parse(buf, len, &f);
  CHECK_EQ(rc, NE_OK);
  if (rc == NE_OK) {
    test_header(&f);
    test_names(&f);
    test_segments(&f);
    test_relocs(&f);
    test_expanded_sites(&f);
    test_osfixup_sites(buf, &f);
    test_import_sites(buf, &f);
    test_segment35(&f);
    test_entries(&f);
    test_resources(&f);
    test_dialogs(buf, len, &f);
    test_bitmap(buf, len, &f);
  }
  ne_free(&f);

  test_truncated(buf);
  test_bad_input();
  test_synthetic();
  free(buf);
  return TEST_DONE();
}
