/* KERNEL part 2 (task stars-0z4.3). Behaviour from the Windows 3.1 SDK and MS-DOS 6.22 INT 21h;
 * Wine's dlls/krnl386.exe16 as reference. Plain C, no harness dependency. */
#include "runtime/kernel_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/mem.h"
#include "runtime/vfs.h"

int kernel_dos_strict;

/* ---- far string helpers ---- */

/* Copies the NUL-terminated string at p into out (cap bytes). Returns its length, or -1 when
 * p is NULL, not addressable, or does not fit. */
static int far_cstr(FARPTR p, char *out, size_t cap) {
  if (!p) return -1;
  for (size_t i = 0; i < cap; i++) {
    FARPTR q = MK_FP(FP_SEG(p), (uint16_t)(FP_OFF(p) + i));
    if (!mem_check(q, 1)) return -1;
    out[i] = *(const char *)LIN(q);
    if (!out[i]) return (int)i;
  }
  return -1;
}

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++) {
    int x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 0;
  }
  return *a == *b;
}

/* ---- OpenFile and the _l functions ---- */

static void ofs_status(uint8_t *ofs, uint16_t err) {
  ofs[OFS_NERRCODE] = (uint8_t)(err & 0xFF);
  ofs[OFS_NERRCODE + 1] = (uint8_t)(err >> 8);
}

HFILE16 OpenFile(FARPTR name, FARPTR reopen, uint16_t style) {
  uint8_t *ofs;
  char raw[160], full[VFS_PATH_MAX];
  int err;
  uint16_t h;
  if (!mem_check(reopen, OFSTRUCT_SIZE)) return HFILE_ERROR;
  ofs = LIN(reopen);
  if (style & OF_REOPEN) {
    memcpy(raw, ofs + OFS_PATHNAME, OFS_PATHNAME_LEN);
    raw[OFS_PATHNAME_LEN - 1] = 0;
  } else if (far_cstr(name, raw, sizeof raw) < 0) {
    ofs_status(ofs, 2);
    return HFILE_ERROR;
  }
  ofs[OFS_CBYTES] = OFSTRUCT_SIZE;
  ofs[OFS_FFIXEDDISK] = 1;
  if ((err = vfs_resolve(raw, full, 0))) {
    ofs_status(ofs, (uint16_t)err);
    return HFILE_ERROR;
  }
  memset(ofs + OFS_PATHNAME, 0, OFS_PATHNAME_LEN);
  memcpy(ofs + OFS_PATHNAME, full, strlen(full));
  ofs_status(ofs, 0);
  if (style & OF_PARSE) return 0;
  /* UNVERIFIED: OF_DELETE and OF_EXIST succeed with 1, from memory; the game is believed
   * never to use these flags. */
  if (style & OF_DELETE) {
    err = vfs_unlink(full);
    ofs_status(ofs, (uint16_t)err);
    return err ? HFILE_ERROR : 1;
  }
  if ((style & OF_EXIST) && !(style & OF_CREATE)) {
    err = vfs_open(full, VFS_READ, 0, &h);
    if (!err) vfs_close(h);
    ofs_status(ofs, (uint16_t)err);
    return err ? HFILE_ERROR : 1;
  }
  /* OF_CREATE opens read/write whatever the access bits say; OF_SHARE_* is ignored */
  err = vfs_open(full, (style & OF_CREATE) ? VFS_READWRITE : (style & 3) == 3 ? VFS_READ : (style & 3),
                 (style & OF_CREATE) != 0, &h);
  if (err) {
    ofs_status(ofs, (uint16_t)err);
    return HFILE_ERROR;
  }
  if (style & OF_EXIST) { /* OF_EXIST | OF_CREATE: create, close, report */
    vfs_close(h);
    return 1;
  }
  return (HFILE16)h;
}

HFILE16 _lclose(HFILE16 hf) { return vfs_close((uint16_t)hf) ? HFILE_ERROR : 0; }

/* A buffer may run past the 64 KB limit of a selector of a huge global block (the game reads
 * resource bitmaps of over 100 KB in 30000-byte chunks at offsets such as 0xEA60, or at 0xD4C0
 * of the block's second selector): DOS reads into protected-mode memory by linear address, so it
 * succeeds as long as the whole range lies inside the block. The selectors of a block are
 * linearly contiguous (step MEM_AHINCR), which is checked chunk by chunk. */
static bool buf_ok(FARPTR buf, uint16_t n) {
  uint16_t sel = FP_SEG(buf);
  uint32_t off = FP_OFF(buf), left = n;
  for (;;) {
    uint32_t room = 0x10000u - off;
    if (left <= room) return mem_check(MK_FP(sel, off), left);
    if (!mem_check(MK_FP(sel, off), room)) return false;
    if (!mem_check(MK_FP((uint16_t)(sel + MEM_AHINCR), 0), 1)) return false;
    if ((uint8_t *)LIN(MK_FP((uint16_t)(sel + MEM_AHINCR), 0)) != (uint8_t *)LIN(MK_FP(sel, 0)) + 0x10000u)
      return false;
    left -= room;
    sel = (uint16_t)(sel + MEM_AHINCR);
    off = 0;
  }
}

uint16_t _lread(HFILE16 hf, FARPTR buf, uint16_t n) {
  uint32_t got;
  if (n && !buf_ok(buf, n)) return 0xFFFF;
  if (vfs_read((uint16_t)hf, n ? LIN(buf) : NULL, n, &got)) return 0xFFFF;
  return (uint16_t)got;
}

uint16_t _lwrite(HFILE16 hf, FARPTR buf, uint16_t n) {
  uint32_t put;
  if (n && !buf_ok(buf, n)) return 0xFFFF;
  if (vfs_write((uint16_t)hf, n ? LIN(buf) : NULL, n, &put)) return 0xFFFF;
  return (uint16_t)put;
}

/* ---- DOS3Call ---- */

#define CF 0x0001u

static void fail(KDosRegs *r, int err) {
  r->flags |= CF;
  r->ax = (uint16_t)err;
}
static void ok(KDosRegs *r) { r->flags &= (uint16_t)~CF; }

/* The ASCIIZ path at DS:DX. */
static int dx_path(const KDosRegs *r, char *out, size_t cap) {
  return far_cstr(MK_FP(r->ds, r->dx), out, cap);
}

/* The DTA, defaulting to the PSP command tail (PSP:0x80) as DOS does. */
static FARPTR dta(void) {
  FARPTR d = vfs_get_dta();
  return d ? d : MK_FP(kernel_psp_selector(), 0x80);
}

/* Find-first/next record in the 43-byte DTA (DOS layout: 0-20 reserved for the search,
 * 21 attribute, 22 time, 24 date, 26 size, 30 ASCIIZ name). Reserved part here: byte 0
 * attribute mask, 1-13 pattern, 14 directory node, 16 next node. */
static int dta_store(FARPTR d, const VfsSearch *s, const VfsEnt *e) {
  uint8_t *p;
  if (!mem_check(d, 43)) return 0;
  p = LIN(d);
  memset(p, 0, 43);
  p[0] = s->attr;
  memcpy(p + 1, s->pattern, 13);
  p[14] = (uint8_t)(s->dir & 0xFF);
  p[15] = (uint8_t)(s->dir >> 8);
  p[16] = (uint8_t)(s->next & 0xFF);
  p[17] = (uint8_t)(s->next >> 8);
  p[21] = e->attr;
  p[22] = (uint8_t)(e->time & 0xFF);
  p[23] = (uint8_t)(e->time >> 8);
  p[24] = (uint8_t)(e->date & 0xFF);
  p[25] = (uint8_t)(e->date >> 8);
  p[26] = (uint8_t)(e->size & 0xFF);
  p[27] = (uint8_t)(e->size >> 8);
  p[28] = (uint8_t)(e->size >> 16);
  p[29] = (uint8_t)(e->size >> 24);
  memcpy(p + 30, e->name, strlen(e->name) + 1);
  return 1;
}

static void dos_find(KDosRegs *r, int first) {
  VfsSearch s;
  VfsEnt e;
  FARPTR d = dta();
  int err;
  if (first) {
    char path[160];
    if (dx_path(r, path, sizeof path) < 0) return fail(r, 3);
    err = vfs_find_first(path, (uint8_t)r->cx, &s, &e);
  } else {
    const uint8_t *p;
    if (!mem_check(d, 43)) return fail(r, 18);
    p = LIN(d);
    s.attr = p[0];
    memcpy(s.pattern, p + 1, 13);
    s.dir = (uint16_t)(p[14] | (p[15] << 8));
    s.next = (uint16_t)(p[16] | (p[17] << 8));
    err = vfs_find_next(&s, &e);
  }
  if (err) return fail(r, err);
  if (!dta_store(d, &s, &e)) return fail(r, 18);
  ok(r);
}

void DOS3Call(KDosRegs *r) {
  uint8_t ah = (uint8_t)(r->ax >> 8), al = (uint8_t)r->ax;
  char path[160];
  int err;
  switch (ah) {
  case 0x1A: /* set DTA = DS:DX */
    vfs_set_dta(MK_FP(r->ds, r->dx));
    break;
  case 0x2F: { /* get DTA -> ES:BX */
    FARPTR d = dta();
    r->es = FP_SEG(d);
    r->bx = FP_OFF(d);
    break;
  }
  case 0x25: /* set interrupt vector AL = DS:DX */
    vfs_set_vector(al, MK_FP(r->ds, r->dx));
    break;
  case 0x35: { /* get interrupt vector AL -> ES:BX */
    FARPTR v = vfs_get_vector(al);
    r->es = FP_SEG(v);
    r->bx = FP_OFF(v);
    break;
  }
  case 0x2A: { /* get date: CX year, DH month, DL day, AL weekday */
    int y, mo, d;
    rt_clock_get(&y, &mo, &d, NULL, NULL, NULL);
    r->cx = (uint16_t)y;
    r->dx = (uint16_t)((mo << 8) | d);
    r->ax = (uint16_t)((r->ax & 0xFF00) | rt_clock_weekday());
    break;
  }
  case 0x2C: { /* get time: CH hour, CL minute, DH second, DL hundredths */
    int h, mi, s;
    rt_clock_get(NULL, NULL, NULL, &h, &mi, &s);
    r->cx = (uint16_t)((h << 8) | mi);
    r->dx = (uint16_t)(s << 8);
    break;
  }
  case 0x30: /* DOS version: AL major, AH minor, BH OEM (MS-DOS), BL:CX serial */
    r->ax = (uint16_t)(((KERNEL_DOS_VERSION_WORD & 0xFF) << 8) | (KERNEL_DOS_VERSION_WORD >> 8));
    r->bx = 0xFF00;
    r->cx = 0;
    break;
  case 0x36: /* free disk space: DL drive (0 default, 3 = C:) */
    if ((r->dx & 0xFF) != 0 && (r->dx & 0xFF) != 3) {
      r->ax = 0xFFFF; /* invalid drive: AX = FFFFh, CF unchanged */
      return;
    }
    /* UNVERIFIED: geometry needs a reference capture. The game uses the total size for a
     * machine fingerprint at DS:0x564c. */
    r->ax = 32;    /* sectors per cluster */
    r->bx = 16384; /* free clusters: 256 MB of 512 MB */
    r->cx = 512;   /* bytes per sector */
    r->dx = 32768; /* total clusters */
    break;
  case 0x39:
  case 0x3A:
  case 0x3B:
  case 0x41:
    if (dx_path(r, path, sizeof path) < 0) return fail(r, 3);
    err = ah == 0x39 ? vfs_mkdir(path) : ah == 0x3A ? vfs_rmdir(path)
        : ah == 0x3B ? vfs_chdir(path) : vfs_unlink(path);
    if (err) return fail(r, err);
    break;
  case 0x40: { /* write CX bytes from DS:DX to handle BX */
    uint32_t put;
    FARPTR buf = MK_FP(r->ds, r->dx);
    if (r->bx < VFS_FIRST_HANDLE) { /* standard devices: output is discarded */
      if (r->bx == 0) return fail(r, 5);
      r->ax = r->cx;
      break;
    }
    if (r->cx && !mem_check(buf, r->cx)) return fail(r, 5);
    if ((err = vfs_write(r->bx, r->cx ? LIN(buf) : NULL, r->cx, &put))) return fail(r, err);
    r->ax = (uint16_t)put;
    break;
  }
  /* UNVERIFIED: vfs_seek reports error 25 for a seek before the start; that code is a guess. */
  case 0x42: { /* lseek: AL whence, BX handle, CX:DX offset -> DX:AX position */
    uint32_t pos;
    if ((err = vfs_seek(r->bx, (int32_t)(((uint32_t)r->cx << 16) | r->dx), al, &pos)))
      return fail(r, err);
    r->ax = (uint16_t)pos;
    r->dx = (uint16_t)(pos >> 16);
    break;
  }
  case 0x43: { /* attributes: AL 0 get -> CX, AL 1 set from CX */
    if (dx_path(r, path, sizeof path) < 0) return fail(r, 3);
    if (al == 0) {
      uint8_t a;
      if ((err = vfs_getattr(path, &a))) return fail(r, err);
      r->cx = a;
    } else if (al == 1) {
      if ((err = vfs_setattr(path, (uint8_t)r->cx))) return fail(r, err);
    } else {
      return fail(r, 1);
    }
    break;
  }
  case 0x4C: /* terminate with return code AL */
    kernel_exit_code = al;
    kernel_exit_pending = 1;
    break;
  case 0x4E:
    dos_find(r, 1);
    return;
  case 0x4F:
    dos_find(r, 0);
    return;
  case 0x56: { /* rename DS:DX -> ES:DI */
    char to[160];
    if (dx_path(r, path, sizeof path) < 0 || far_cstr(MK_FP(r->es, r->di), to, sizeof to) < 0)
      return fail(r, 3);
    if ((err = vfs_rename(path, to))) return fail(r, err);
    break;
  }
  default: {
    char msg[64];
    if (kernel_dos_strict) {
      snprintf(msg, sizeof msg, "DOS3Call: unimplemented INT 21h AH=0x%02X", ah);
      kernel_fatal(msg);
    }
    return fail(r, 1);
  }
  }
  ok(r);
}

/* ---- private profile (INI) files ---- */

typedef struct {
  const char *s;
  size_t len;  /* without the line terminator */
  size_t full; /* with it */
} Line;

static int next_line(const char *buf, size_t n, size_t *pos, Line *l) {
  size_t i = *pos;
  if (i >= n) return 0;
  l->s = buf + i;
  while (i < n && buf[i] != '\n') i++;
  l->full = i < n ? i + 1 - *pos : i - *pos;
  l->len = i - *pos;
  if (l->len && l->s[l->len - 1] == '\r') l->len--;
  *pos += l->full;
  return 1;
}

static void trim(const char **s, size_t *len) {
  while (*len && (**s == ' ' || **s == '\t')) {
    (*s)++;
    (*len)--;
  }
  while (*len && ((*s)[*len - 1] == ' ' || (*s)[*len - 1] == '\t')) (*len)--;
}

static int span_ieq(const char *s, size_t len, const char *z) {
  size_t i;
  if (strlen(z) != len) return 0;
  for (i = 0; i < len; i++) {
    char a[2] = {s[i], 0}, b[2] = {z[i], 0};
    if (!ieq(a, b)) return 0;
  }
  return 1;
}

/* "[name]": returns 1 and the trimmed name span. */
static int is_header(const Line *l, const char **name, size_t *nlen) {
  const char *s = l->s;
  size_t len = l->len, end;
  trim(&s, &len);
  if (!len || s[0] != '[') return 0;
  for (end = 1; end < len && s[end] != ']'; end++) {}
  if (end >= len) return 0;
  *name = s + 1;
  *nlen = end - 1;
  trim(name, nlen);
  return 1;
}

/* "key=value" (not a comment or header): the trimmed key and value spans. */
static int is_keyval(const Line *l, const char **k, size_t *klen, const char **v, size_t *vlen) {
  const char *s = l->s;
  size_t len = l->len, eq;
  trim(&s, &len);
  if (!len || s[0] == ';' || s[0] == '[') return 0;
  for (eq = 0; eq < len && s[eq] != '='; eq++) {}
  if (eq >= len) return 0;
  *k = s;
  *klen = eq;
  trim(k, klen);
  *v = s + eq + 1;
  *vlen = len - eq - 1;
  trim(v, vlen);
  return 1;
}

static int ini_path(FARPTR lpFile, char *out) {
  char name[96];
  if (far_cstr(lpFile, name, sizeof name) <= 0) return 0;
  if (strpbrk(name, "\\/:")) return vfs_resolve(name, out, 0) == 0;
  return vfs_resolve("C:\\WINDOWS", out, 0) == 0 && strlen(out) + strlen(name) + 2 < VFS_PATH_MAX &&
         (strcat(out, "\\"), strcat(out, name), 1);
}

/* Looks up [app] key in the file. Returns 1 and the value span when present. */
static int ini_find(const uint8_t *buf, size_t n, const char *app, const char *key, const char **v,
                    size_t *vlen) {
  size_t pos = 0;
  Line l;
  int in = 0;
  while (next_line((const char *)buf, n, &pos, &l)) {
    const char *nm, *k;
    size_t nl, kl;
    if (is_header(&l, &nm, &nl)) {
      in = span_ieq(nm, nl, app);
    } else if (in && is_keyval(&l, &k, &kl, v, vlen) && span_ieq(k, kl, key)) {
      return 1;
    }
  }
  return 0;
}

static void get_file(const char *path, const uint8_t **buf, size_t *n) {
  static const uint8_t empty[1];
  if (vfs_get(path, buf, n)) {
    *buf = empty;
    *n = 0;
  }
}

uint16_t GetPrivateProfileInt(FARPTR lpApp, FARPTR lpKey, int16_t nDefault, FARPTR lpFile) {
  char app[128], key[128], path[VFS_PATH_MAX], num[32];
  const uint8_t *buf;
  const char *v;
  size_t n, vlen;
  long val = 0;
  int neg = 0;
  size_t i = 0;
  if (far_cstr(lpApp, app, sizeof app) < 0 || far_cstr(lpKey, key, sizeof key) < 0 ||
      !ini_path(lpFile, path))
    return (uint16_t)nDefault;
  get_file(path, &buf, &n);
  if (!ini_find(buf, n, app, key, &v, &vlen)) return (uint16_t)nDefault;
  if (vlen >= sizeof num) vlen = sizeof num - 1;
  memcpy(num, v, vlen);
  num[vlen] = 0;
  if (num[i] == '-') {
    neg = 1;
    i++;
  }
  /* Clamp like Wine16: positive values to 65535, negative to -32768. NOTE: recalled from
   * memory of Wine's behaviour, not verified against a reference. */
  for (; num[i] >= '0' && num[i] <= '9'; i++) {
    val = val * 10 + (num[i] - '0');
    if (val > 0xFFFFFFL) val = 0xFFFFFFL; /* stop growing; far beyond either limit */
  }
  if (neg) return (uint16_t)(val > 32768 ? -32768 : -val);
  return (uint16_t)(val > 65535 ? 65535 : val);
}

/* NULL section / key enumeration is not implemented: nothing in the game's call sites has
 * been shown to use it, so a NULL app or key returns the default like a missing key. */
uint16_t GetPrivateProfileString(FARPTR lpApp, FARPTR lpKey, FARPTR lpDefault, FARPTR lpRet,
                                 uint16_t nSize, FARPTR lpFile) {
  char app[128], key[128], def[1024], path[VFS_PATH_MAX];
  const uint8_t *buf;
  const char *v = def;
  size_t n, vlen;
  if (!nSize || !mem_check(lpRet, nSize)) return 0;
  if (lpDefault && far_cstr(lpDefault, def, sizeof def) < 0) def[0] = 0;
  if (!lpDefault) def[0] = 0;
  vlen = strlen(def);
  if (far_cstr(lpApp, app, sizeof app) >= 0 && far_cstr(lpKey, key, sizeof key) >= 0 &&
      ini_path(lpFile, path)) {
    get_file(path, &buf, &n);
    if (!ini_find(buf, n, app, key, &v, &vlen)) {
      v = def;
      vlen = strlen(def);
    }
  }
  if (vlen > (size_t)nSize - 1) vlen = (size_t)nSize - 1;
  memcpy(LIN(lpRet), v, vlen);
  ((char *)LIN(lpRet))[vlen] = 0;
  return (uint16_t)vlen;
}

/* Rewrites the file: the line of [app] key is replaced (or appended at the end of the section,
 * before its trailing blank lines), the section is created at the end of the file when
 * missing. lpString NULL deletes the key, lpKey NULL deletes the whole section. Every other
 * line is copied unchanged; new lines end in CR LF. */
uint16_t WritePrivateProfileString(FARPTR lpApp, FARPTR lpKey, FARPTR lpString, FARPTR lpFile) {
  char app[128], key[128], val[1024], path[VFS_PATH_MAX];
  const uint8_t *old;
  size_t n, pos = 0, olen = 0, ins = 0, cap;
  uint8_t *out;
  Line l;
  int in = 0, found_sec = 0, done = 0, del_sec, del_key;
  if (far_cstr(lpApp, app, sizeof app) < 0 || !ini_path(lpFile, path)) return 0;
  del_sec = lpKey == 0;
  del_key = !del_sec && lpString == 0;
  if (!del_sec && far_cstr(lpKey, key, sizeof key) < 0) return 0;
  if (!del_sec && !del_key && far_cstr(lpString, val, sizeof val) < 0) return 0;
  get_file(path, &old, &n);
  /* the old text + a CR LF for a last line without one + the new line or section */
  cap = n + 2 + strlen(app) + (del_sec ? 0 : strlen(key) + (del_key ? 0 : strlen(val))) + 16;
  out = malloc(cap);
  if (!out) return 0;
  while (next_line((const char *)old, n, &pos, &l)) {
    const char *nm, *k, *v;
    size_t nl, kl, vl;
    int hdr = is_header(&l, &nm, &nl);
    if (hdr) {
      if (in && !done && !del_sec && !del_key) { /* section ends: add the new key */
        size_t add = strlen(key) + strlen(val) + 3;
        memmove(out + ins + add, out + ins, olen - ins);
        memcpy(out + ins, key, strlen(key));
        out[ins + strlen(key)] = '=';
        memcpy(out + ins + strlen(key) + 1, val, strlen(val));
        memcpy(out + ins + strlen(key) + 1 + strlen(val), "\r\n", 2);
        olen += add;
        done = 1;
      }
      in = span_ieq(nm, nl, app);
      if (in) found_sec = 1;
    }
    if (in && del_sec) continue;
    if (in && !hdr && !del_sec && !done && is_keyval(&l, &k, &kl, &v, &vl) && span_ieq(k, kl, key)) {
      if (del_key) continue;
      memcpy(out + olen, key, strlen(key));
      olen += strlen(key);
      out[olen++] = '=';
      memcpy(out + olen, val, strlen(val));
      olen += strlen(val);
      out[olen++] = '\r';
      out[olen++] = '\n';
      ins = olen;
      done = 1;
      continue;
    }
    memcpy(out + olen, l.s, l.full); /* the line and its own terminator, unchanged */
    olen += l.full;
    if (l.s[l.full - 1] != '\n') {
      out[olen++] = '\r';
      out[olen++] = '\n';
    }
    if (in) {
      size_t tl = l.len;
      const char *ts = l.s;
      trim(&ts, &tl);
      /* UNVERIFIED: the blank line before an appended section follows from this rule; it is
       * not checked against a reference (the game reads back only its own values). */
      if (tl) ins = olen; /* after the last non-blank line of the section */
    }
  }
  if (!done && !del_sec && !del_key) {
    if (found_sec) { /* section was the last one */
      size_t add = strlen(key) + strlen(val) + 3;
      memmove(out + ins + add, out + ins, olen - ins);
      memcpy(out + ins, key, strlen(key));
      out[ins + strlen(key)] = '=';
      memcpy(out + ins + strlen(key) + 1, val, strlen(val));
      memcpy(out + ins + strlen(key) + 1 + strlen(val), "\r\n", 2);
      olen += add;
    } else {
      olen += (size_t)snprintf((char *)out + olen, cap - olen, "[%s]\r\n%s=%s\r\n", app, key, val);
    }
  }
  n = (size_t)vfs_put(path, out, olen);
  free(out);
  return n == 0 ? 1 : 0;
}
