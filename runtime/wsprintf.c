/* _wsprintf (USER.420), task stars-0z4.7. Plain C, no harness dependency.
 *
 * Format set (issue): %d %i %u %x %X %c %s, the l prefix for 32-bit integers, %%, the flags '-'
 * and '0', a width. No floating point. %s takes a far string. The arguments are read from the
 * 16-bit stack image lpArgs: 16-bit integers and chars are one word, longs and far pointers are
 * two words, low word first. The output is limited to 1024 bytes including the terminating NUL;
 * the return value is the number of characters stored.
 *
 * Behaviour follows Wine (wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
 * dlls/user.exe16/user.c): parse_format 191-244, wvsprintf16 2482-2585, wsprintf16 2588-2590. Beyond the issue's set
 * Wine also accepts '#' (0x prefix for %x), '.precision' (minimum digits, or maximum string
 * length) and the h prefix: kept, UNVERIFIED for Windows 3.1. Other UNVERIFIED rules, all from
 * Wine: %c of a NUL stores nothing; a NULL or unreadable far string prints nothing; an unknown
 * conversion drops the '%' and its flags and prints the conversion character as text. The
 * 1024-byte limit is the issue's and not in Wine. */
#include <string.h>

#include "runtime/user_internal.h"

#define WS_LIMIT 1024

typedef struct Fmt {
  bool left, prefix, zero, lng, upper;
  unsigned width, prec;
  char type; /* 'c' 's' 'd' 'u' 'x' or 0 for unknown */
} Fmt;

typedef struct Args {
  FARPTR p;
} Args;

static uint16_t arg16(Args *a) {
  uint16_t v = 0;
  if (a->p && mem_check(a->p, 2)) memcpy(&v, LIN(a->p), 2);
  a->p = MK_FP(FP_SEG(a->p), (uint16_t)(FP_OFF(a->p) + 2));
  return v;
}
static uint32_t arg32(Args *a) {
  uint32_t lo = arg16(a);
  uint32_t hi = arg16(a);
  return lo | (hi << 16);
}

/* Returns the number of format characters consumed after the '%'. */
static unsigned parse_format(const char *p0, Fmt *f) {
  const char *p = p0;
  memset(f, 0, sizeof *f);
  if (*p == '-') { f->left = true; p++; }
  if (*p == '#') { f->prefix = true; p++; }
  if (*p == '0') { f->zero = true; p++; }
  while (*p >= '0' && *p <= '9') f->width = f->width * 10 + (unsigned)(*p++ - '0');
  if (*p == '.') {
    p++;
    while (*p >= '0' && *p <= '9') f->prec = f->prec * 10 + (unsigned)(*p++ - '0');
  }
  if (*p == 'l') { f->lng = true; p++; }
  else if (*p == 'h') p++;
  switch (*p) {
    case 'c': case 'C': f->type = 'c'; break;
    case 's': case 'S': f->type = 's'; break;
    case 'd': case 'i': f->type = 'd'; break;
    case 'u': f->type = 'u'; break;
    case 'p': f->width = 8; f->zero = true; f->upper = true; f->type = 'x'; break; /* Wine extension */
    case 'X': f->upper = true; f->type = 'x'; break;
    case 'x': f->type = 'x'; break;
    default: f->type = 0; p--; break; /* unknown: the character is printed as text */
  }
  return (unsigned)(p - p0) + 1;
}

static unsigned utoa_base(char *out, uint32_t v, unsigned base, bool upper) {
  char tmp[16];
  unsigned n = 0, k = 0;
  const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  do {
    tmp[n++] = digits[v % base];
    v /= base;
  } while (v);
  while (n) out[k++] = tmp[--n];
  out[k] = 0;
  return k;
}

int16_t _wsprintf(FARPTR lpOut, FARPTR lpFmt, FARPTR lpArgs) {
  char buf[WS_LIMIT], *out;
  const char *fmt;
  unsigned n = 0;
  Args a = {lpArgs};
  if (!lpOut || !lpFmt || !mem_check(lpOut, 1) || !mem_check(lpFmt, 1)) return 0;
  out = buf; /* formatted aside, then copied once the whole length is known to fit lpOut */
  fmt = LIN(lpFmt);
#define PUT(c)                                    \
  do {                                            \
    if (n < WS_LIMIT - 1) out[n] = (char)(c);     \
    if (n < WS_LIMIT - 1) n++;                    \
  } while (0)
  while (*fmt) {
    Fmt f;
    char number[24];
    unsigned len = 0, sign = 0, prec;
    const char *sview = NULL;
    char cview = 0;
    if (*fmt != '%') { PUT(*fmt++); continue; }
    fmt++;
    if (*fmt == '%') { PUT(*fmt++); continue; }
    fmt += parse_format(fmt, &f);
    switch (f.type) {
      case 'c':
        cview = (char)arg16(&a);
        len = f.prec = 1;
        break;
      case 's': {
        FARPTR sp = arg32(&a);
        if (!sp || !mem_check(sp, 1)) {
          sview = "";
        } else {
          sview = LIN(sp);
        }
        for (len = 0; (!f.prec || len < f.prec) && (sp ? mem_check(sp + len, 1) : true) && sview[len]; len++) {}
        f.prec = len;
        break;
      }
      case 'd': {
        int32_t v = f.lng ? (int32_t)arg32(&a) : (int32_t)(int16_t)arg16(&a);
        uint32_t u = v < 0 ? (uint32_t)0 - (uint32_t)v : (uint32_t)v;
        if (v < 0) number[len++] = '-';
        len += utoa_base(number + len, u, 10, false);
        break;
      }
      case 'u': {
        uint32_t v = f.lng ? arg32(&a) : arg16(&a);
        len = utoa_base(number, v, 10, false);
        break;
      }
      case 'x': {
        uint32_t v = f.lng ? arg32(&a) : arg16(&a);
        len = utoa_base(number, v, 16, f.upper);
        break;
      }
      default:
        continue; /* unknown: the conversion character follows as ordinary text */
    }
    if (f.prec < len) f.prec = len;
    if (f.left) f.zero = false;
    if (f.zero && f.width > f.prec) f.prec = f.width;
    if (f.prefix && f.type == 'x') len += 2;
    prec = f.prec;
    if (!f.left)
      for (unsigned i = prec; i < f.width; i++) PUT(' ');
    switch (f.type) {
      case 'c':
        if (cview) PUT(cview);
        else if (f.width > 1) PUT(' ');
        break;
      case 's':
        for (unsigned i = 0; i < len; i++) PUT(sview[i]);
        break;
      default: /* numbers */
        if (f.type == 'x' && f.prefix) {
          PUT('0');
          PUT(f.upper ? 'X' : 'x');
          len -= 2;
        }
        if (f.type != 'u' && number[0] == '-') {
          PUT('-');
          sign = 1;
        }
        for (unsigned i = len; i < prec; i++) PUT('0');
        for (unsigned i = sign; i < len; i++) PUT(number[i]);
        break;
    }
    if (f.left)
      for (unsigned i = prec; i < f.width; i++) PUT(' ');
  }
#undef PUT
  out[n] = 0;
  /* The text and its NUL must lie inside the caller's segment: nothing is written otherwise
   * (UNVERIFIED for Windows 3.1, which would simply overrun). */
  if (!mem_check(lpOut, n + 1)) return 0;
  memcpy(LIN(lpOut), buf, n + 1);
  return (int16_t)n;
}
