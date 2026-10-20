/* Virtual file system (task stars-0z4.3). See runtime/vfs.h. */
#include "runtime/vfs.h"

#include <string.h>

#include "runtime/mem.h"

#define MAX_NODES 256
#define MAX_HANDLES 32
#define MAX_COMPS 16
#define STATE_MAGIC 0x53564653u /* "SVFS" */

typedef struct {
  char path[VFS_PATH_MAX]; /* full upper-case path, "C:\" for the root */
  uint8_t used, is_dir, attr, pad;
  uint32_t size, cap, data; /* data: arena offset of the bytes, 0 when cap is 0 */
  uint16_t time, date;
  uint16_t pad2;
} Node;

typedef struct {
  uint8_t used, mode;
  uint16_t node;
  uint32_t pos;
} Hnd;

/* All of it lives in one arena block; only its offset is kept in a static. */
typedef struct {
  uint32_t magic;
  int16_t year, month, day, hour, minute, second;
  uint32_t dta;
  char cwd[VFS_PATH_MAX];
  uint32_t vec[256];
  Hnd h[MAX_HANDLES];
  Node nodes[MAX_NODES];
} State;

static uint32_t g_off;
static const uint8_t *g_base;

static void free_all_data(State *s) {
  for (int i = 0; i < MAX_NODES; i++)
    if (s->nodes[i].used && s->nodes[i].cap) rt_free(mem_arena() + s->nodes[i].data);
}

static uint16_t dos_time(const State *s) {
  return (uint16_t)((s->hour << 11) | (s->minute << 5) | (s->second / 2));
}
static uint16_t dos_date(const State *s) {
  return (uint16_t)(((s->year - 1980) << 9) | (s->month << 5) | s->day);
}

static int node_new(State *s, const char *path, int is_dir) {
  for (int i = 0; i < MAX_NODES; i++) {
    Node *n = &s->nodes[i];
    if (n->used) continue;
    memset(n, 0, sizeof *n);
    n->used = 1;
    n->is_dir = (uint8_t)(is_dir != 0);
    n->attr = is_dir ? VFS_ATTR_DIR : VFS_ATTR_ARCHIVE;
    n->time = dos_time(s);
    n->date = dos_date(s);
    strcpy(n->path, path);
    return i;
  }
  return -1;
}

static State *init_state(void) {
  State *s = rt_alloc(sizeof *s);
  if (!s) return NULL;
  memset(s, 0, sizeof *s);
  s->magic = STATE_MAGIC;
  s->year = 1980;
  s->month = 1;
  s->day = 1;
  strcpy(s->cwd, "C:\\STARS");
  node_new(s, "C:\\", 1);
  node_new(s, "C:\\WINDOWS", 1);
  node_new(s, "C:\\STARS", 1);
  g_off = mem_off(s);
  g_base = mem_arena();
  return s;
}

static State *st(void) {
  if (g_off && g_base != mem_arena()) g_off = 0; /* the arena was replaced: stale offset */
  if (g_off && ((State *)(mem_arena() + g_off))->magic != STATE_MAGIC) g_off = 0; /* arena reused: stale */
  if (!g_off) return init_state();
  return (State *)(mem_arena() + g_off);
}

void vfs_init(void) {
  vfs_shutdown();
  if (mem_arena()) init_state();
}

void vfs_shutdown(void) {
  if (g_off && g_base == mem_arena() && mem_arena()) {
    State *s = (State *)(mem_arena() + g_off);
    free_all_data(s);
    rt_free(s);
  }
  g_off = 0;
  g_base = NULL;
}

/* ---- clock ---- */

void rt_clock_set(int year, int month, int day, int hour, int minute, int second) {
  State *s = st();
  if (!s) return;
  s->year = (int16_t)year;
  s->month = (int16_t)month;
  s->day = (int16_t)day;
  s->hour = (int16_t)hour;
  s->minute = (int16_t)minute;
  s->second = (int16_t)second;
}

void rt_clock_get(int *y, int *mo, int *d, int *h, int *mi, int *sec) {
  State *s = st();
  if (!s) return;
  if (y) *y = s->year;
  if (mo) *mo = s->month;
  if (d) *d = s->day;
  if (h) *h = s->hour;
  if (mi) *mi = s->minute;
  if (sec) *sec = s->second;
}

/* Days since 1970-01-01 (civil-from-days inverse, Howard Hinnant); 1970-01-01 was a Thursday. */
int rt_clock_weekday(void) {
  State *s = st();
  long y, m, d, era, yoe, doy, doe, days;
  if (!s) return 0;
  y = s->year;
  m = s->month;
  d = s->day;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  days = era * 146097 + doe - 719468;
  return (int)(((days % 7) + 7 + 4) % 7);
}

uint32_t rt_clock_ms(void) {
  State *s = st();
  int64_t y, m, d, era, yoe, doy, doe, days;
  if (!s) return 0;
  y = s->year;
  m = s->month;
  d = s->day;
  y -= m <= 2;
  era = (y >= 0 ? y : y - 399) / 400;
  yoe = y - era * 400;
  doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  days = era * 146097 + doe - 719468 - 3652; /* 1970-01-01 -> 1980-01-01 is 3652 days */
  return (uint32_t)((((days * 24 + s->hour) * 60 + s->minute) * 60 + s->second) * 1000);
}

/* ---- path handling ---- */

static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int is_sep(char c) { return c == '\\' || c == '/'; }

static int name_char_ok(char c, int wild) {
  if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return 1;
  if (wild && (c == '*' || c == '?')) return 1;
  return c != 0 && strchr("!#$%&'()-@^_`{}~", c) != NULL;
}

/* A component is valid 8.3 (upper-cased already). */
static int comp_ok(const char *c, int wild) {
  size_t i = 0, name = 0, ext = 0;
  int dot = 0;
  if (!*c) return 0;
  for (; c[i]; i++) {
    if (c[i] == '.') {
      if (dot || i == 0) return 0;
      dot = 1;
    } else {
      if (!name_char_ok(c[i], wild)) return 0;
      if (dot) ext++;
      else name++;
    }
  }
  return name <= 8 && ext <= 3 && !(dot && ext == 0);
}

/* Relative names resolve against C:\STARS only, which is where the game's files live (differs
 * from Windows only when a file exists solely in C:\WINDOWS). */
int vfs_resolve(const char *in, char *out, int wild) {
  char comps[MAX_COMPS][14];
  int n = 0;
  const char *p = in;
  const State *s = st();
  if (!s || !in) return 3;
  if (p[0] && p[1] == ':') {
    if (up(p[0]) != 'C') return 15;
    p += 2;
  }
  if (!is_sep(*p)) {
    /* relative: start from the current directory */
    const char *c = s->cwd + 3;
    while (*c) {
      size_t k = 0;
      while (c[k] && c[k] != '\\') k++;
      if (n >= MAX_COMPS || k > 12) return 3;
      memcpy(comps[n], c, k);
      comps[n][k] = 0;
      n++;
      c += k + (c[k] ? 1 : 0);
    }
  }
  while (*p) {
    char tok[14];
    size_t k = 0;
    while (is_sep(*p)) p++;
    if (!*p) break;
    while (p[k] && !is_sep(p[k])) k++;
    if (k > 12) return 3;
    for (size_t i = 0; i < k; i++) tok[i] = (char)up(p[i]);
    tok[k] = 0;
    p += k;
    if (!strcmp(tok, ".")) continue;
    if (!strcmp(tok, "..")) {
      if (n == 0) return 3;
      n--;
      continue;
    }
    /* a wildcard is allowed in the last component only */
    if (n > 0 && (strchr(comps[n - 1], '*') || strchr(comps[n - 1], '?'))) return 3;
    if (!comp_ok(tok, wild) || n >= MAX_COMPS) return 3;
    strcpy(comps[n++], tok);
  }
  strcpy(out, "C:\\");
  for (int i = 0; i < n; i++) {
    if (strlen(out) + strlen(comps[i]) + 2 > VFS_PATH_MAX) return 3;
    if (i) strcat(out, "\\");
    strcat(out, comps[i]);
  }
  return 0;
}

static int find_node(const State *s, const char *path) {
  for (int i = 0; i < MAX_NODES; i++)
    if (s->nodes[i].used && !strcmp(s->nodes[i].path, path)) return i;
  return -1;
}

/* Writes the parent path of a full path into out; returns 0 for the root. */
static int parent_of(const char *path, char *out) {
  size_t len = strlen(path), i;
  if (len <= 3) return 0;
  for (i = len; i > 0 && path[i - 1] != '\\'; i--) {}
  memcpy(out, path, i > 3 ? i - 1 : 3);
  out[i > 3 ? i - 1 : 3] = 0;
  return 1;
}

static const char *base_of(const char *path) {
  const char *b = strrchr(path, '\\');
  return b ? b + 1 : path;
}

/* Missing node: 3 when its parent directory is missing too, else 2. */
static int missing_err(const State *s, const char *path) {
  char par[VFS_PATH_MAX];
  int pi;
  if (!parent_of(path, par)) return 2;
  pi = find_node(s, par);
  return (pi >= 0 && s->nodes[pi].is_dir) ? 2 : 3;
}

static int handle_uses(const State *s, int node) {
  for (int i = 0; i < MAX_HANDLES; i++)
    if (s->h[i].used && s->h[i].node == node) return 1;
  return 0;
}

/* ---- file data ---- */

static int ensure_cap(Node *n, uint32_t need) {
  uint32_t nc;
  uint8_t *p;
  if (need <= n->cap) return 1;
  nc = n->cap ? n->cap * 2 : 256;
  if (nc < need) nc = need;
  p = rt_alloc(nc);
  if (!p) return 0;
  if (n->size) memcpy(p, mem_arena() + n->data, n->size);
  if (n->cap) rt_free(mem_arena() + n->data);
  n->data = mem_off(p);
  n->cap = nc;
  return 1;
}

/* Sets the size, zero-filling a growth. */
static int set_size(Node *n, uint32_t size) {
  if (size > n->size) {
    if (!ensure_cap(n, size)) return 0;
    memset(mem_arena() + n->data + n->size, 0, size - n->size);
  }
  n->size = size;
  return 1;
}

static void touch(const State *s, Node *n) {
  n->time = dos_time(s);
  n->date = dos_date(s);
  n->attr |= VFS_ATTR_ARCHIVE;
}

static void node_free(Node *n) {
  if (n->cap) rt_free(mem_arena() + n->data);
  memset(n, 0, sizeof *n);
}

/* ---- host interface ---- */

int vfs_put(const char *path, const void *data, size_t len) {
  State *s = st();
  char full[VFS_PATH_MAX], pre[VFS_PATH_MAX];
  int ni;
  Node *n;
  if (!s || len > 0x7FFFFFFFu || vfs_resolve(path, full, 0)) return -1;
  /* create missing parent directories */
  for (const char *c = full + 3; *c; c++) {
    if (*c != '\\') continue;
    memcpy(pre, full, (size_t)(c - full));
    pre[c - full] = 0;
    ni = find_node(s, pre);
    if (ni < 0) {
      if (node_new(s, pre, 1) < 0) return -1;
    } else if (!s->nodes[ni].is_dir) {
      return -1;
    }
  }
  ni = find_node(s, full);
  if (ni >= 0 && s->nodes[ni].is_dir) return -1;
  if (ni < 0) ni = node_new(s, full, 0);
  if (ni < 0) return -1;
  n = &s->nodes[ni];
  n->size = 0;
  if (!set_size(n, (uint32_t)len)) return -1;
  if (len) memcpy(mem_arena() + n->data, data, len);
  touch(s, n);
  return 0;
}

int vfs_get(const char *path, const uint8_t **data, size_t *len) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni;
  if (!s || vfs_resolve(path, full, 0)) return -1;
  ni = find_node(s, full);
  if (ni < 0 || s->nodes[ni].is_dir) return -1;
  if (data) *data = mem_arena() + (s->nodes[ni].cap ? s->nodes[ni].data : 0);
  if (len) *len = s->nodes[ni].size;
  return 0;
}

static int is_child(const char *dir, const char *path) {
  char par[VFS_PATH_MAX];
  return parent_of(path, par) && !strcmp(par, dir);
}

int vfs_list(const char *dir, VfsListFn cb, void *ud) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int di, count = 0;
  if (!s || vfs_resolve(dir, full, 0)) return -1;
  di = find_node(s, full);
  if (di < 0 || !s->nodes[di].is_dir) return -1;
  for (int i = 0; i < MAX_NODES; i++) {
    const Node *n = &s->nodes[i];
    if (!n->used || !is_child(full, n->path)) continue;
    if (cb) cb(base_of(n->path), n->is_dir, n->size, ud);
    count++;
  }
  return count;
}

/* ---- handles ---- */

int vfs_open(const char *path, int mode, int create, uint16_t *h) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err, hi;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0 && !create) return missing_err(s, full);
  for (hi = 0; hi < MAX_HANDLES && s->h[hi].used; hi++) {}
  if (hi == MAX_HANDLES) return 4;
  if (ni < 0) {
    char par[VFS_PATH_MAX];
    int pi;
    if (!parent_of(full, par) || (pi = find_node(s, par)) < 0 || !s->nodes[pi].is_dir) return 3;
    if ((ni = node_new(s, full, 0)) < 0) return 5;
  } else {
    Node *n = &s->nodes[ni];
    if (n->is_dir) return 5;
    if ((mode != VFS_READ || create) && (n->attr & VFS_ATTR_RO)) return 5;
    if (create) {
      n->size = 0;
      touch(s, n);
    }
  }
  s->h[hi].used = 1;
  s->h[hi].mode = (uint8_t)mode;
  s->h[hi].node = (uint16_t)ni;
  s->h[hi].pos = 0;
  *h = (uint16_t)(hi + VFS_FIRST_HANDLE);
  return 0;
}

static Hnd *hnd(State *s, uint16_t h) {
  if (!s || h < VFS_FIRST_HANDLE || h >= VFS_FIRST_HANDLE + MAX_HANDLES) return NULL;
  return s->h[h - VFS_FIRST_HANDLE].used ? &s->h[h - VFS_FIRST_HANDLE] : NULL;
}

int vfs_read(uint16_t h, void *buf, uint32_t n, uint32_t *got) {
  State *s = st();
  Hnd *p = hnd(s, h);
  Node *nd;
  uint32_t avail;
  if (!p) return 6;
  if (p->mode == VFS_WRITE) return 5;
  nd = &s->nodes[p->node];
  avail = p->pos < nd->size ? nd->size - p->pos : 0;
  if (n > avail) n = avail;
  if (n) memcpy(buf, mem_arena() + nd->data + p->pos, n);
  p->pos += n;
  *got = n;
  return 0;
}

int vfs_write(uint16_t h, const void *buf, uint32_t n, uint32_t *put) {
  State *s = st();
  Hnd *p = hnd(s, h);
  Node *nd;
  if (!p) return 6;
  if (p->mode == VFS_READ) return 5;
  nd = &s->nodes[p->node];
  if (n == 0) {
    /* DOS: a write of zero bytes truncates (or extends) the file at the pointer */
    if (!set_size(nd, p->pos)) return 5;
    touch(s, nd);
    *put = 0;
    return 0;
  }
  if ((uint64_t)p->pos + n > 0x7FFFFFFFu) return 5;
  if (p->pos + n > nd->size && !set_size(nd, p->pos + n)) return 5;
  memcpy(mem_arena() + nd->data + p->pos, buf, n);
  p->pos += n;
  touch(s, nd);
  *put = n;
  return 0;
}

int vfs_seek(uint16_t h, int32_t off, int whence, uint32_t *newpos) {
  State *s = st();
  Hnd *p = hnd(s, h);
  int64_t base, pos;
  if (!p) return 6;
  if (whence == 0) base = 0;
  else if (whence == 1) base = p->pos;
  else if (whence == 2) base = s->nodes[p->node].size;
  else return 1;
  pos = base + off;
  if (pos < 0 || pos > 0x7FFFFFFFLL) return 25; /* UNVERIFIED: error 25 here is a guess */
  p->pos = (uint32_t)pos;
  *newpos = p->pos;
  return 0;
}

int vfs_close(uint16_t h) {
  State *s = st();
  Hnd *p = hnd(s, h);
  if (!p) return 6;
  memset(p, 0, sizeof *p);
  return 0;
}

int vfs_handle_size(uint16_t h, uint32_t *size) {
  State *s = st();
  Hnd *p = hnd(s, h);
  if (!p) return 6;
  *size = s->nodes[p->node].size;
  return 0;
}

/* ---- namespace operations ---- */

int vfs_unlink(const char *path) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0) return missing_err(s, full);
  if (s->nodes[ni].is_dir || (s->nodes[ni].attr & VFS_ATTR_RO) || handle_uses(s, ni)) return 5;
  node_free(&s->nodes[ni]);
  return 0;
}

int vfs_mkdir(const char *path) {
  State *s = st();
  char full[VFS_PATH_MAX], par[VFS_PATH_MAX];
  int pi, err;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  if (!parent_of(full, par)) return 5; /* the root exists */
  if (find_node(s, full) >= 0) return 5;
  pi = find_node(s, par);
  if (pi < 0 || !s->nodes[pi].is_dir) return 3;
  return node_new(s, full, 1) < 0 ? 5 : 0;
}

int vfs_rmdir(const char *path) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0 || !s->nodes[ni].is_dir) return 3;
  if (!strcmp(full, "C:\\") || !strcmp(full, s->cwd)) return 5;
  for (int i = 0; i < MAX_NODES; i++)
    if (s->nodes[i].used && is_child(full, s->nodes[i].path)) return 5;
  node_free(&s->nodes[ni]);
  return 0;
}

int vfs_chdir(const char *path) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err;
  if (!s) return 3;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0 || !s->nodes[ni].is_dir) return 3;
  strcpy(s->cwd, full);
  return 0;
}

void vfs_getcwd(char *out) { strcpy(out, st()->cwd); }

int vfs_rename(const char *from, const char *to) {
  State *s = st();
  char a[VFS_PATH_MAX], b[VFS_PATH_MAX], par[VFS_PATH_MAX];
  int ai, pi, err;
  size_t la;
  if (!s) return 5;
  if ((err = vfs_resolve(from, a, 0)) || (err = vfs_resolve(to, b, 0))) return err;
  ai = find_node(s, a);
  if (ai < 0) return missing_err(s, a);
  if (!parent_of(b, par)) return 5;
  pi = find_node(s, par);
  if (pi < 0 || !s->nodes[pi].is_dir) return 3;
  if (find_node(s, b) >= 0 && strcmp(a, b)) return 5;
  if (!strcmp(a, "C:\\") || handle_uses(s, ai) || !strcmp(a, s->cwd)) return 5;
  la = strlen(a);
  if (!strncmp(b, a, la) && b[la] == '\\') return 5; /* into itself */
  /* first pass: every resulting path must fit, so a failure leaves the tree unchanged */
  if (s->nodes[ai].is_dir) {
    for (int i = 0; i < MAX_NODES; i++) {
      const Node *n = &s->nodes[i];
      if (n->used && i != ai && !strncmp(n->path, a, la) && n->path[la] == '\\' &&
          strlen(b) + strlen(n->path + la) + 1 > VFS_PATH_MAX)
        return 5;
    }
  }
  /* the node and, for a directory, everything below it */
  for (int i = 0; i < MAX_NODES; i++) {
    Node *n = &s->nodes[i];
    char tmp[VFS_PATH_MAX];
    if (!n->used) continue;
    if (i == ai) {
      strcpy(n->path, b);
    } else if (s->nodes[ai].is_dir && !strncmp(n->path, a, la) && n->path[la] == '\\') {
      strcpy(tmp, b);
      strcat(tmp, n->path + la);
      strcpy(n->path, tmp);
    }
  }
  return 0;
}

int vfs_getattr(const char *path, uint8_t *attr) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0) return missing_err(s, full);
  *attr = s->nodes[ni].attr;
  return 0;
}

int vfs_setattr(const char *path, uint8_t attr) {
  State *s = st();
  char full[VFS_PATH_MAX];
  int ni, err;
  if (!s) return 5;
  if ((err = vfs_resolve(path, full, 0))) return err;
  ni = find_node(s, full);
  if (ni < 0) return missing_err(s, full);
  if (s->nodes[ni].is_dir) return 5;
  s->nodes[ni].attr = (uint8_t)(attr & 0x27);
  return 0;
}

/* ---- directory search ---- */

static int glob(const char *p, const char *t) {
  if (!*p) return !*t;
  if (*p == '*') {
    for (;; t++) {
      if (glob(p + 1, t)) return 1;
      if (!*t) return 0;
    }
  }
  if (!*t) return 0;
  return (*p == '?' || *p == *t) && glob(p + 1, t + 1);
}

/* DOS wildcard match on name.ext: both parts are matched separately. A pattern with a * and
 * no dot ("A*", "*") matches any extension, as DOS does. */
static int wild_match(const char *pat, const char *name) {
  char pn[16], pe[16], nn[16], ne[16];
  const char *d;
  size_t k;
  d = strchr(pat, '.');
  k = d ? (size_t)(d - pat) : strlen(pat);
  memcpy(pn, pat, k);
  pn[k] = 0;
  strcpy(pe, d ? d + 1 : (strchr(pat, '*') ? "*" : ""));
  d = strchr(name, '.');
  k = d ? (size_t)(d - name) : strlen(name);
  memcpy(nn, name, k);
  nn[k] = 0;
  strcpy(ne, d ? d + 1 : "");
  return glob(pn, nn) && glob(pe, ne);
}

int vfs_find_next(VfsSearch *se, VfsEnt *e) {
  State *s = st();
  if (!s || se->dir >= MAX_NODES || !s->nodes[se->dir].used) return 18;
  for (; se->next < MAX_NODES; se->next++) {
    const Node *n = &s->nodes[se->next];
    const char *nm;
    if (!n->used || !is_child(s->nodes[se->dir].path, n->path)) continue;
    nm = base_of(n->path);
    if (!wild_match(se->pattern, nm)) continue;
    if ((n->attr & (VFS_ATTR_HIDDEN | VFS_ATTR_SYSTEM | VFS_ATTR_DIR)) & ~se->attr) continue;
    strcpy(e->name, nm);
    e->attr = n->attr;
    e->time = n->time;
    e->date = n->date;
    e->size = n->is_dir ? 0 : n->size;
    se->next++;
    return 0;
  }
  return 18;
}

int vfs_find_first(const char *spec, uint8_t attr, VfsSearch *se, VfsEnt *e) {
  State *s = st();
  char full[VFS_PATH_MAX], par[VFS_PATH_MAX];
  int di, err, r;
  if (!s) return 3;
  if ((err = vfs_resolve(spec, full, 1))) return err;
  if (!parent_of(full, par)) return 3;
  di = find_node(s, par);
  if (di < 0 || !s->nodes[di].is_dir) return 3;
  strcpy(se->pattern, base_of(full));
  se->attr = attr;
  se->dir = (uint16_t)di;
  se->next = 0;
  /* attribute exactly 0x08 = volume-label search; the VFS has no labels. The game's
   * machine-fingerprint code (8:0xb709) depends on this. */
  if (attr == 0x08) return 18;
  r = vfs_find_next(se, e);
  return r == 18 ? 2 : r;
}

/* ---- DOS state ---- */

uint32_t vfs_get_dta(void) { return st()->dta; }
void vfs_set_dta(uint32_t fp) { st()->dta = fp; }
uint32_t vfs_get_vector(uint8_t n) { return st()->vec[n]; }
void vfs_set_vector(uint8_t n, uint32_t fp) { st()->vec[n] = fp; }
