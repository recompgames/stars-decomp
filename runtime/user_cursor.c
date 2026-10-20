/* Windows 3.1 look, cursors and icons (task stars-0z4.9).
 *
 * Cursors: LoadCursor, DestroyCursor, ShowCursor and a SOFTWARE cursor composited at present time
 * (user_present): for every cursor pixel, AND mask first, then XOR mask: AND 1 / XOR 0 leaves the
 * pixel, AND 0 / XOR 0 is black, AND 0 / XOR 1 is white, AND 1 / XOR 1 inverts the colour. The
 * hot spot is the pixel under the pointer. The framebuffer is not touched.
 * Icons: LoadIcon, DrawIcon, DestroyIcon (AND mask with SRCAND, then the colour image with
 * SRCINVERT, through a compatible bitmap).
 *
 * Sources. The game's own cursors and icons are read from the loaded module's GROUP_CURSOR /
 * GROUP_ICON resources (Windows 3.x 16-bit DIB format: NEWHEADER + RESDIR, then per image a hot
 * spot (cursors) and a BITMAPINFOHEADER with doubled height, palette, XOR image, AND mask; 1 and
 * 4 bit images). The system cursors (IDC_*) and icons (IDI_*) are drawn in code: no Microsoft
 * file is used, so their shapes are look-alikes and UNVERIFIED against Windows 3.1
 * (docs/inventory.md section 13, "Windows 3.1 look"). Plain C. */
#include <string.h>

#include "runtime/kernel.h"
#include "runtime/user_internal.h"

/* ---- 32x32 art canvas for the system cursors ---- */

typedef struct Art {
  uint8_t s[32][32];    /* black body */
  uint8_t inv[32][32];  /* inverting pixels (AND 1, XOR 1) */
  int hx, hy;
} Art;

static void a_pt(Art *a, int x, int y) {
  if (x >= 0 && x < 32 && y >= 0 && y < 32) a->s[y][x] = 1;
}
static void a_line(Art *a, int x0, int y0, int x1, int y1) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
  for (;;) {
    a_pt(a, x0, y0);
    if (x0 == x1 && y0 == y1) break;
    {
      int e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x0 += sx; }
      if (e2 < dx) { err += dx; y0 += sy; }
    }
  }
}
static void a_rect(Art *a, int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) a_pt(a, x, y);
}
static void a_head(Art *a, int x, int y, int dx, int dy) {   /* arrowhead, apex (x,y), pointing (dx,dy) */
  for (int k = 0; k < 5; k++)
    for (int p = -k; p <= k; p++) {
      if (dx) a_pt(a, x - dx * k, y + p);
      else a_pt(a, x + p, y - dy * k);
    }
}

static const char *ARROW[] = {
    "X...........", "XX..........", "XXX.........", "XXXX........", "XXXXX.......", "XXXXXX......",
    "XXXXXXX.....", "XXXXXXXX....", "XXXXXXXXX...", "XXXXXXXXXX..", "XXXXXXXXXXX.", "XXXXXX......",
    "XX.XXXX.....", "X..XXXX.....", "....XXXX....", "....XXXX....", ".....XXXX...", ".....XXXX..."};

static void art_build(int id, Art *a) {
  memset(a, 0, sizeof *a);
  switch (id) {
    case 32512: /* IDC_ARROW */
      for (unsigned y = 0; y < sizeof ARROW / sizeof ARROW[0]; y++)
        for (int x = 0; ARROW[y][x]; x++)
          if (ARROW[y][x] == 'X') a_pt(a, x, (int)y);
      a->hx = 0; a->hy = 0;
      break;
    case 32513: /* IDC_IBEAM: inverts what is under it */
      for (int y = 1; y <= 18; y++) { a->inv[y][6] = 1; a->inv[y][7] = 1; }
      for (int x = 3; x <= 10; x++) { a->inv[0][x] = 1; a->inv[19][x] = 1; }
      a->hx = 6; a->hy = 9;
      break;
    case 32514: /* IDC_WAIT: hourglass */
      a_rect(a, 8, 0, 23, 1);
      a_rect(a, 8, 21, 23, 22);
      a_line(a, 9, 2, 9, 20); a_line(a, 22, 2, 22, 20);
      a_line(a, 9, 2, 15, 11); a_line(a, 22, 2, 16, 11);
      a_line(a, 15, 11, 9, 20); a_line(a, 16, 11, 22, 20);
      a->hx = 15; a->hy = 11;
      break;
    case 32515: /* IDC_CROSS */
      a_line(a, 15, 2, 15, 12); a_line(a, 15, 18, 15, 28);
      a_line(a, 2, 15, 12, 15); a_line(a, 18, 15, 28, 15);
      a->hx = 15; a->hy = 15;
      break;
    case 32516: /* IDC_UPARROW */
      a_head(a, 7, 0, 0, -1);
      a_rect(a, 6, 5, 8, 24);
      a->hx = 7; a->hy = 0;
      break;
    case 32640: /* IDC_SIZE: four-way */
      a_line(a, 5, 15, 26, 15); a_line(a, 15, 5, 15, 26);
      a_head(a, 3, 15, -1, 0); a_head(a, 28, 15, 1, 0); a_head(a, 15, 3, 0, -1); a_head(a, 15, 28, 0, 1);
      a->hx = 15; a->hy = 15;
      break;
    case 32641: /* IDC_ICON */
      a_line(a, 4, 4, 20, 4); a_line(a, 4, 20, 20, 20); a_line(a, 4, 4, 4, 20); a_line(a, 20, 4, 20, 20);
      a->hx = 0; a->hy = 0;
      break;
    case 32642: /* IDC_SIZENWSE */
      a_line(a, 7, 7, 24, 24); a_line(a, 8, 7, 25, 24);
      a_line(a, 5, 5, 12, 5); a_line(a, 5, 5, 5, 12); a_line(a, 5, 6, 11, 6); a_line(a, 6, 5, 6, 11);
      a_line(a, 26, 26, 19, 26); a_line(a, 26, 26, 26, 19); a_line(a, 26, 25, 20, 25); a_line(a, 25, 26, 25, 20);
      a->hx = 15; a->hy = 15;
      break;
    case 32643: /* IDC_SIZENESW */
      a_line(a, 24, 7, 7, 24); a_line(a, 23, 7, 6, 24);
      a_line(a, 26, 5, 19, 5); a_line(a, 26, 5, 26, 12); a_line(a, 26, 6, 20, 6); a_line(a, 25, 5, 25, 11);
      a_line(a, 5, 26, 12, 26); a_line(a, 5, 26, 5, 19); a_line(a, 5, 25, 11, 25); a_line(a, 6, 26, 6, 20);
      a->hx = 15; a->hy = 15;
      break;
    case 32644: /* IDC_SIZEWE */
      a_line(a, 5, 15, 26, 15); a_line(a, 5, 16, 26, 16);
      a_head(a, 2, 15, -1, 0); a_head(a, 29, 15, 1, 0);
      a->hx = 15; a->hy = 15;
      break;
    case 32645: /* IDC_SIZENS */
      a_line(a, 15, 5, 15, 26); a_line(a, 16, 5, 16, 26);
      a_head(a, 15, 2, 0, -1); a_head(a, 15, 29, 0, 1);
      a->hx = 15; a->hy = 15;
      break;
    default: art_build(32512, a); break;
  }
}

static void art_to_cursor(const Art *a, UCursorData *d) {
  memset(d, 0, sizeof *d);
  d->hx = (int16_t)a->hx;
  d->hy = (int16_t)a->hy;
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      bool body = a->s[y][x], white = false;
      int and_ = 1, xor_ = 0;
      if (a->inv[y][x]) { and_ = 1; xor_ = 1; }
      else if (body) { and_ = 0; xor_ = 0; }
      else {
        for (int dy = -1; dy <= 1 && !white; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < 32 && ny >= 0 && ny < 32 && a->s[ny][nx]) { white = true; break; }
          }
        if (white) { and_ = 0; xor_ = 1; }
      }
      if (and_) d->andm[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
      if (xor_) d->xorm[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
    }
}

static void sys_cursor(int id, UCursorData *d) {
  Art a;
  art_build(id, &a);
  art_to_cursor(&a, d);
}

/* ---- resources ---- */

#define RES_CURSOR 1
#define RES_ICON 3
#define RES_GROUP_CURSOR 12
#define RES_GROUP_ICON 14

/* The module is the loaded program when its first resource can be sized. */
static bool module_ok(HANDLE16 hinst) { return hinst != 0 && SizeofResource(hinst, 1) != 0; }

static const uint8_t *res_get(HANDLE16 hinst, FARPTR name, uint16_t type, uint32_t *size, uint16_t *hdata) {
  HANDLE16 hr = FindResource(hinst, name, (FARPTR)type);
  FARPTR p;
  if (!hr) return NULL;
  *size = SizeofResource(hinst, hr);
  *hdata = LoadResource(hinst, hr);
  if (!*hdata) return NULL;
  p = LockResource(*hdata);
  return p ? (const uint8_t *)LIN(p) : NULL;
}

static void res_put(uint16_t hdata) {
  if (!hdata) return;
  GlobalUnlock(hdata);
  FreeResource(hdata);
}

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* The id of the first image of a GROUP_CURSOR / GROUP_ICON (RESDIR entries are 14 bytes, the id
 * is the last word). */
static int group_first_id(HANDLE16 hinst, FARPTR name, uint16_t gtype) {
  uint32_t n = 0;
  uint16_t hd = 0;
  const uint8_t *g = res_get(hinst, name, gtype, &n, &hd);
  int id = -1;
  if (g && n >= 20 && le16(g + 4) >= 1) id = le16(g + 6 + 12);
  res_put(hd);
  return id;
}

typedef struct Dib {
  int w, h, bpp, npal;
  const uint8_t *pal, *xorp, *andp;
  int xs, as;
} Dib;

static bool dib_parse(const uint8_t *d, uint32_t n, int skip, Dib *o) {
  const uint8_t *bi = d + skip;
  uint32_t size, clr;
  int hh;
  if (n < (uint32_t)skip + 40) return false;
  size = le32(bi);
  o->w = (int)le32(bi + 4);
  hh = (int)le32(bi + 8);
  o->bpp = le16(bi + 14);
  clr = le32(bi + 32);
  if (size < 40 || o->w <= 0 || o->w > 64 || hh <= 0 || hh > 128 || (o->bpp != 1 && o->bpp != 4)) return false;
  o->h = hh / 2;
  o->npal = clr ? (int)clr : (1 << o->bpp);
  if (o->npal > 16) return false;
  o->pal = bi + size;
  o->xorp = o->pal + o->npal * 4;
  o->xs = ((o->w * o->bpp + 31) / 32) * 4;
  o->as = ((o->w + 31) / 32) * 4;
  o->andp = o->xorp + (size_t)o->xs * (size_t)o->h;
  if (o->andp + (size_t)o->as * (size_t)o->h > d + n) return false;
  return true;
}

static int dib_index(const Dib *b, int x, int y) { /* y counted from the top */
  const uint8_t *row = b->xorp + (size_t)(b->h - 1 - y) * (size_t)b->xs;
  if (b->bpp == 4) return (x & 1) ? (row[x / 2] & 15) : (row[x / 2] >> 4);
  return (row[x / 8] >> (7 - x % 8)) & 1;
}
static int dib_and(const Dib *b, int x, int y) {
  const uint8_t *row = b->andp + (size_t)(b->h - 1 - y) * (size_t)b->as;
  return (row[x / 8] >> (7 - x % 8)) & 1;
}
static uint32_t dib_rgb(const Dib *b, int idx) {
  const uint8_t *e = b->pal + idx * 4;
  return GDI_RGB(e[2], e[1], e[0]);
}

static bool load_cursor_resource(HANDLE16 hinst, FARPTR name, UCursorData *d) {
  int id = group_first_id(hinst, name, RES_GROUP_CURSOR);
  uint32_t n = 0;
  uint16_t hd = 0;
  const uint8_t *r;
  Dib b;
  bool ok = false;
  if (id < 0) return false;
  r = res_get(hinst, (FARPTR)(uint32_t)id, RES_CURSOR, &n, &hd);
  if (r && n >= 4 && dib_parse(r, n, 4, &b)) {
    memset(d, 0, sizeof *d);
    d->hx = (int16_t)le16(r);
    d->hy = (int16_t)le16(r + 2);
    for (int y = 0; y < 32; y++)
      for (int x = 0; x < 32; x++) {
        int and_ = 1, xor_ = 0;
        if (x < b.w && y < b.h) {
          uint32_t c = dib_rgb(&b, dib_index(&b, x, y));
          int lum = (int)(((c & 255) * 30 + ((c >> 8) & 255) * 59 + ((c >> 16) & 255) * 11) / 100);
          and_ = dib_and(&b, x, y);
          xor_ = lum >= 128 && c != 0;
        }
        if (and_) d->andm[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
        if (xor_) d->xorm[y * 4 + x / 8] |= (uint8_t)(0x80 >> (x % 8));
      }
    ok = true;
  }
  res_put(hd);
  return ok;
}

/* ---- cursors ---- */

#define IDC_SYS_BASE 0x0C00u  /* handles 0x0C01..: system cursors; 0x0C40..: application cursors */
#define IDC_APP_BASE 0x0C40u
static const uint16_t SYS_CURSORS[] = {32512, 32513, 32514, 32515, 32516, 32640, 32641, 32642, 32643, 32644, 32645};

static uint32_t name_key(FARPTR name) {
  uint32_t h = 2166136261u;
  const char *s;
  if (FP_SEG(name) == 0) return 0x80000000u | FP_OFF(name); /* MAKEINTRESOURCE */
  if (!mem_check(name, 1)) return 0;
  s = LIN(name);
  for (unsigned i = 0; i < 80 && mem_check(name + i, 1) && s[i]; i++) {
    char c = s[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    h = (h ^ (uint8_t)c) * 16777619u;
  }
  return (h & 0x7FFFFFFFu) | 1u;
}

HCURSOR16 LoadCursor(HANDLE16 hInstance, FARPTR lpCursorName) {
  UserState *st = user_state();
  uint32_t key;
  UCursorData d;
  if (!st || !lpCursorName) return 0;
  if (hInstance == 0 && FP_SEG(lpCursorName) == 0) {
    for (unsigned i = 0; i < sizeof SYS_CURSORS / sizeof SYS_CURSORS[0]; i++)
      if (SYS_CURSORS[i] == FP_OFF(lpCursorName)) return (HCURSOR16)(IDC_SYS_BASE + 1 + i);
  }
  key = name_key(lpCursorName);
  if (!key) return 0;
  for (unsigned i = 0; i < UCUR_MAX; i++)
    if (st->in.cursors[i].used && st->in.cursors[i].hinst == hInstance && st->in.cursors[i].key == key)
      return (HCURSOR16)(IDC_APP_BASE + i);
  if (!load_cursor_resource(hInstance, lpCursorName, &d)) {
    if (module_ok(hInstance)) return 0;    /* the program has no such cursor */
    sys_cursor(32512, &d);                 /* another module: a handle with the arrow shape */
  }
  for (unsigned i = 0; i < UCUR_MAX; i++)
    if (!st->in.cursors[i].used) {
      st->in.cursors[i].used = 1;
      st->in.cursors[i].hinst = hInstance;
      st->in.cursors[i].key = key;
      st->curdata[i] = d;
      return (HCURSOR16)(IDC_APP_BASE + i);
    }
  return 0;
}

uint16_t DestroyCursor(HCURSOR16 h) {
  UserState *st = user_state();
  if (!st || h < IDC_APP_BASE || h >= IDC_APP_BASE + UCUR_MAX) return 0;
  if (!st->in.cursors[h - IDC_APP_BASE].used) return 0;
  st->in.cursors[h - IDC_APP_BASE].used = 0;
  return 1;
}

int16_t ShowCursor(uint16_t bShow) {
  UserState *st = user_state();
  if (!st) return 0;
  st->cursor_count = (int16_t)(st->cursor_count + (bShow ? 1 : -1));
  return st->cursor_count;
}

static bool cursor_data(HCURSOR16 h, UCursorData *d) {
  UserState *st = user_state();
  if (!st) return false;
  if (h > IDC_SYS_BASE && h <= IDC_SYS_BASE + sizeof SYS_CURSORS / sizeof SYS_CURSORS[0]) {
    sys_cursor(SYS_CURSORS[h - IDC_SYS_BASE - 1], d);
    return true;
  }
  if (h >= IDC_APP_BASE && h < IDC_APP_BASE + UCUR_MAX && st->in.cursors[h - IDC_APP_BASE].used) {
    *d = st->curdata[h - IDC_APP_BASE];
    return true;
  }
  return false;
}

bool user_cursor_masks(HCURSOR16 h, uint8_t and_mask[1024], uint8_t xor_mask[1024], int16_t *hot_x, int16_t *hot_y) {
  UCursorData d;
  if (!cursor_data(h, &d)) return false;
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      uint8_t bit = (uint8_t)(0x80 >> (x % 8));
      and_mask[y * 32 + x] = (d.andm[y * 4 + x / 8] & bit) != 0;
      xor_mask[y * 32 + x] = (d.xorm[y * 4 + x / 8] & bit) != 0;
    }
  if (hot_x) *hot_x = d.hx;
  if (hot_y) *hot_y = d.hy;
  return true;
}

void user_present(uint32_t *out) {
  UserState *st = user_state();
  UCursorData d;
  fb_present(out);
  if (!st || st->cursor_count < 0 || !st->in.has_pos || !cursor_data(st->in.cursor, &d)) return;
  for (int y = 0; y < 32; y++)
    for (int x = 0; x < 32; x++) {
      uint8_t bit = (uint8_t)(0x80 >> (x % 8));
      int a = (d.andm[y * 4 + x / 8] & bit) != 0, b = (d.xorm[y * 4 + x / 8] & bit) != 0;
      int sx = st->in.px - d.hx + x, sy = st->in.py - d.hy + y;
      uint32_t *p;
      if (sx < 0 || sy < 0 || sx >= fb_width() || sy >= fb_height()) continue;
      p = &out[sy * fb_width() + sx];
      if (a && !b) continue;
      if (!a && !b) *p = 0xFF000000u;
      else if (!a && b) *p = 0xFFFFFFFFu;
      else *p = 0xFF000000u | (~*p & 0x00FFFFFFu);
    }
}

/* ---- icons ---- */

static void ic_clear(UIcon *ic) {
  memset(ic->px, 0, sizeof ic->px);
  memset(ic->andm, 1, sizeof ic->andm);
}
static void ic_set(UIcon *ic, int x, int y, int idx) {
  if (x < 0 || x >= 32 || y < 0 || y >= 32) return;
  ic->px[y * 32 + x] = (uint8_t)idx;
  ic->andm[y * 32 + x] = 0;
}
static void ic_rect(UIcon *ic, int x0, int y0, int x1, int y1, int idx) {
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) ic_set(ic, x, y, idx);
}
static void ic_ellipse(UIcon *ic, int cx, int cy, int rx, int ry, int fill, int edge) {
  for (int y = cy - ry - 1; y <= cy + ry + 1; y++)
    for (int x = cx - rx - 1; x <= cx + rx + 1; x++) {
      long dx = x - cx, dy = y - cy;
      long v = dx * dx * (long)ry * ry + dy * dy * (long)rx * rx, full = (long)rx * rx * ry * ry;
      long ox = rx - 1, oy = ry - 1;
      long vi = dx * dx * oy * oy + dy * dy * ox * ox, fi = ox * ox * oy * oy;
      if (v <= full) ic_set(ic, x, y, vi <= fi ? fill : edge);
    }
}
static void ic_glyph(UIcon *ic, int x0, int y0, const char *const *rows, int n, int idx) {
  for (int y = 0; y < n; y++)
    for (int x = 0; rows[y][x]; x++)
      if (rows[y][x] == '#') ic_set(ic, x0 + x, y0 + y, idx);
}

static const char *G_QUESTION[] = {"..####..", ".######.", "##....##", "##....##", "......##", ".....##.", "....##..",
                                   "...##...", "...##...", "........", "...##...", "...##..."};
static const char *G_EXCL[] = {"####", "####", "####", "####", "####", "####", ".##.", ".##.", "....", ".##.", "####", ".##."};
static const char *G_INFO[] = {".##.", ".##.", "....", "###.", ".##.", ".##.", ".##.", ".##.", ".##.", ".##.", "####", "####"};

static void sys_icon(int id, UIcon *ic) {
  /* palette: 0 black, 1 white, 2 navy, 3 red, 4 yellow, 5 light gray, 6 dark gray, 7 blue */
  static const uint32_t PAL[8] = {GDI_RGB(0, 0, 0), GDI_RGB(255, 255, 255), GDI_RGB(0, 0, 128), GDI_RGB(255, 0, 0),
                                  GDI_RGB(255, 255, 0), GDI_RGB(192, 192, 192), GDI_RGB(128, 128, 128), GDI_RGB(0, 0, 255)};
  memset(ic->pal, 0, sizeof ic->pal);
  memcpy(ic->pal, PAL, sizeof PAL);
  ic_clear(ic);
  switch (id) {
    case 32513: /* IDI_HAND: a red stop sign */
      ic_ellipse(ic, 16, 16, 14, 14, 3, 0);
      ic_rect(ic, 8, 13, 23, 18, 1);
      ic_rect(ic, 8, 12, 23, 12, 0);
      ic_rect(ic, 8, 19, 23, 19, 0);
      break;
    case 32514: /* IDI_QUESTION */
    case 32515: /* IDI_EXCLAMATION */
    case 32516: /* IDI_ASTERISK */
      ic_ellipse(ic, 16, 13, 13, 11, id == 32515 ? 4 : 1, 0);
      for (int k = 0; k < 6; k++) {          /* the tail of the speech bubble */
        ic_rect(ic, 8, 23 + k, 13 - k, 23 + k, id == 32515 ? 4 : 1);
        ic_set(ic, 7, 23 + k, 0);
        ic_set(ic, 14 - k, 23 + k, 0);
      }
      ic_rect(ic, 7, 29, 8, 29, 0);
      if (id == 32514) ic_glyph(ic, 12, 7, G_QUESTION, 12, 7);
      else if (id == 32515) ic_glyph(ic, 14, 7, G_EXCL, 12, 0);
      else ic_glyph(ic, 14, 7, G_INFO, 12, 7);
      break;
    default: /* IDI_APPLICATION: a small window */
      ic_rect(ic, 3, 5, 28, 27, 0);
      ic_rect(ic, 4, 6, 27, 26, 1);
      ic_rect(ic, 4, 6, 27, 11, 2);
      ic_rect(ic, 4, 12, 27, 12, 0);
      ic_rect(ic, 8, 16, 22, 17, 5);
      ic_rect(ic, 8, 20, 18, 21, 5);
      ic_rect(ic, 29, 7, 30, 28, 6);
      ic_rect(ic, 5, 28, 30, 29, 6);
      break;
  }
}

static bool load_icon_resource(HANDLE16 hinst, FARPTR name, UIcon *ic) {
  int id = group_first_id(hinst, name, RES_GROUP_ICON);
  uint32_t n = 0;
  uint16_t hd = 0;
  const uint8_t *r;
  Dib b;
  bool ok = false;
  if (id < 0) return false;
  r = res_get(hinst, (FARPTR)(uint32_t)id, RES_ICON, &n, &hd);
  if (r && dib_parse(r, n, 0, &b)) {
    memset(ic->pal, 0, sizeof ic->pal);
    for (int i = 0; i < b.npal; i++) ic->pal[i] = dib_rgb(&b, i);
    ic_clear(ic);
    for (int y = 0; y < 32 && y < b.h; y++)
      for (int x = 0; x < 32 && x < b.w; x++)
        if (!dib_and(&b, x, y)) ic_set(ic, x, y, dib_index(&b, x, y));
    ok = true;
  }
  res_put(hd);
  return ok;
}

static UIcon *icon_of(HICON16 h) {
  UserState *st = user_state();
  if (!st || h < UHICON_BASE || h >= UHICON_BASE + UI_MAX) return NULL;
  return st->icons[h - UHICON_BASE].used ? &st->icons[h - UHICON_BASE] : NULL;
}

HICON16 LoadIcon(HANDLE16 hInstance, FARPTR lpIconName) {
  UserState *st = user_state();
  uint32_t key;
  UIcon tmp;
  bool sys = false;
  int sysid = 32512;
  if (!st || !lpIconName) return 0;
  if (hInstance == 0 && FP_SEG(lpIconName) == 0 && FP_OFF(lpIconName) >= 32512 && FP_OFF(lpIconName) <= 32516) {
    sys = true;
    sysid = FP_OFF(lpIconName);
  }
  key = name_key(lpIconName);
  if (!key) return 0;
  for (unsigned i = 0; i < UI_MAX; i++)
    if (st->icons[i].used && st->icons[i].hinst == hInstance && st->icons[i].key == key) return (HICON16)(UHICON_BASE + i);
  memset(&tmp, 0, sizeof tmp);
  if (sys) sys_icon(sysid, &tmp);
  else if (!load_icon_resource(hInstance, lpIconName, &tmp)) {
    if (module_ok(hInstance)) return 0;
    sys_icon(32512, &tmp);
  }
  for (unsigned i = 0; i < UI_MAX; i++)
    if (!st->icons[i].used) {
      st->icons[i] = tmp;
      st->icons[i].used = 1;
      st->icons[i].hinst = hInstance;
      st->icons[i].key = key;
      return (HICON16)(UHICON_BASE + i);
    }
  return 0;
}

uint16_t DestroyIcon(HICON16 h) {
  UIcon *ic = icon_of(h);
  if (!ic) return 0;
  ic->used = 0;
  return 1;
}

bool user_icon_pixels(HICON16 h, uint32_t rgb[1024], uint8_t and_mask[1024]) {
  const UIcon *ic = icon_of(h);
  if (!ic) return false;
  for (int i = 0; i < 1024; i++) {
    rgb[i] = ic->pal[ic->px[i]];
    and_mask[i] = ic->andm[i];
  }
  return true;
}

uint16_t DrawIcon(HDC16 hdc, int16_t x, int16_t y, HICON16 hIcon) {
  const UIcon *ic = icon_of(hIcon);
  HDC16 mem;
  HBITMAP16 bm, old;
  uint8_t *bits;
  int w, h;
  bool mono;
  if (!ic) return 0;
  mem = CreateCompatibleDC(hdc);
  bm = CreateCompatibleBitmap(hdc, 32, 32);
  if (!mem || !bm) {
    if (bm) DeleteObject(bm);
    if (mem) DeleteDC(mem);
    return 0;
  }
  old = SelectObject(mem, bm);
  bits = gdi_bitmap_bits(bm, &w, &h, &mono);
  if (bits && w == 32 && h == 32 && !mono) {
    for (int i = 0; i < 1024; i++) bits[i] = ic->andm[i] ? 0xFF : 0x00;
    BitBlt(hdc, x, y, 32, 32, mem, 0, 0, SRCAND);
    for (int i = 0; i < 1024; i++) bits[i] = ic->andm[i] ? 0 : gdi_dc_color_index(hdc, ic->pal[ic->px[i]]);
    BitBlt(hdc, x, y, 32, 32, mem, 0, 0, SRCINVERT);
  }
  SelectObject(mem, old);
  DeleteObject(bm);
  DeleteDC(mem);
  return 1;
}

void user_paint_icon_at(HDC16 hdc, int x, int y, HICON16 icon) { DrawIcon(hdc, (int16_t)x, (int16_t)y, icon); }

void user_cursor_init(void) {}
