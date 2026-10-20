/* Expectations for _wsprintf (USER.420), task stars-0z4.7. Written before the implementation.
 * Do not edit an expectation to match the code: a disagreement is reported with both values.
 *
 * The runtime function is _wsprintf(FARPTR lpOut, FARPTR lpFmt, FARPTR lpArgs): lpArgs points at
 * the argument words as they sit on the 16-bit stack after the two fixed pointers (cdecl, first
 * argument lowest; a far pointer or long is two words, offset/low word first).
 *
 * Sources (the Windows 3.1 SDK text is NOT available on this machine, no reference capture exists):
 *   ISSUE  stars-0z4.7: the format set %d %i %u %x %X %c %s %ld %li %lu %lx %lX %%, flags '-' and
 *          '0', a width, no floating point, far %s, an output limit of 1024 bytes, the return
 *          value is the length.
 *   WINE   wine-mirror/wine commit 5b6b85421c2a2161728b2a55e9f09271770a3eb0,
 *          dlls/user.exe16/user.c: parse_format 191-244 (flags - # 0, width, .precision, l/h,
 *          conversion), wvsprintf16 2482-2585 (argument sizes, padding, %c skipping NUL at 2543-2548,
 *          %s of an unreadable pointer prints nothing (2507), unknown conversion prints the
 *          conversion character as plain text, 2490), wsprintf16 2588-2590.
 *
 * UNVERIFIED against Windows 3.1 (also in docs/inventory.md section 13):
 *   - '#', '.precision' and 'h' are Wine extensions to the issue's set; kept because Wine has them.
 *   - The 1024-byte limit: output is cut so that text plus the terminating NUL fit in 1024 bytes
 *     (1023 characters); the return value is the number of characters stored. Wine has no limit.
 *   - An unknown conversion ("%q") prints "q" (the '%' and flags are dropped), as Wine does.
 *   - %c of a NUL stores nothing (width > 1 still pads one blank), as Wine does.
 *   - A NULL or unreadable far string prints nothing, as Wine does.
 */
#include "test.h"

#include "user_test_common.h"

/* user_test_common.h is shared with the window tests; this file needs only part of it. */
__attribute__((unused)) static void use_helpers(void) {
  (void)log_clear; (void)dump; (void)log_find; (void)register_class; (void)mk;
}

/* ---- argument builder ---- */

typedef struct Args {
  uint8_t b[128];
  unsigned n;
} Args;

static void p16(Args *a, uint16_t v) {
  a->b[a->n++] = (uint8_t)v;
  a->b[a->n++] = (uint8_t)(v >> 8);
}
static void p32(Args *a, uint32_t v) {
  p16(a, (uint16_t)v);
  p16(a, (uint16_t)(v >> 16));
}

static FARPTR g_out;

/* Formats fmt with args; returns the _wsprintf result and leaves the text in g_out. */
static int fmt_run(const char *fmt, const Args *a) {
  FARPTR ap = far_alloc(a->n ? a->n : 2);
  memset(LIN(g_out), 0x5A, 1100);
  if (a->n) memcpy(LIN(ap), a->b, a->n);
  return (int16_t)_wsprintf(g_out, fstr(fmt), ap);
}
static const char *out(void) { return (const char *)LIN(g_out); }

#define EXPECT(fmt, args, text)                                      \
  do {                                                               \
    int r_ = fmt_run((fmt), (args));                                 \
    if (strcmp(out(), (text)) != 0) {                                \
      test_checks_run++;                                             \
      test_checks_failed++;                                          \
      fprintf(stderr, "%s:%d: wsprintf(\"%s\") got \"%s\", want \"%s\"\n", __FILE__, __LINE__, (fmt), out(), (text)); \
    } else {                                                         \
      test_checks_run++;                                             \
    }                                                                \
    CHECK_EQ(r_, (int)strlen(text));                                 \
  } while (0)

static void test_issue_cases(void) {
  Args a;
  a = (Args){0}; p16(&a, (uint16_t)-5); EXPECT("%d", &a, "-5");
  a = (Args){0}; p16(&a, 42); EXPECT("%5d", &a, "   42");
  a = (Args){0}; p16(&a, 42); EXPECT("%-5d|", &a, "42   |");
  a = (Args){0}; p16(&a, 42); EXPECT("%05d", &a, "00042");
  a = (Args){0}; p16(&a, 255); EXPECT("%x", &a, "ff");
  a = (Args){0}; p32(&a, 100000UL); EXPECT("%ld", &a, "100000");
  { /* %s takes a far string: offset word then selector word */
    FARPTR s = fstr("abc");
    a = (Args){0}; p32(&a, s); EXPECT("%s", &a, "abc");
  }
  a = (Args){0}; p16(&a, 'A'); EXPECT("%c", &a, "A");
  a = (Args){0}; EXPECT("%%", &a, "%");
  a = (Args){0}; EXPECT("", &a, "");
  a = (Args){0}; EXPECT("plain", &a, "plain");
}

static void test_integers(void) {
  Args a;
  a = (Args){0}; p16(&a, (uint16_t)-5); EXPECT("%i", &a, "-5");
  a = (Args){0}; p16(&a, 0xFFFF); EXPECT("%u", &a, "65535");   /* unsigned 16 bit */
  a = (Args){0}; p16(&a, 0xFFFF); EXPECT("%d", &a, "-1");      /* signed 16 bit */
  a = (Args){0}; p16(&a, 0xBEEF); EXPECT("%X", &a, "BEEF");
  a = (Args){0}; p16(&a, 0xBEEF); EXPECT("%x", &a, "beef");
  a = (Args){0}; p32(&a, (uint32_t)-100000L); EXPECT("%ld", &a, "-100000");
  a = (Args){0}; p32(&a, (uint32_t)-100000L); EXPECT("%li", &a, "-100000");
  a = (Args){0}; p32(&a, 4000000000UL); EXPECT("%lu", &a, "4000000000");
  a = (Args){0}; p32(&a, 0x12345678UL); EXPECT("%lx", &a, "12345678");
  a = (Args){0}; p32(&a, 0xABCDEF01UL); EXPECT("%lX", &a, "ABCDEF01");
  a = (Args){0}; p32(&a, 0xABCDEF01UL); EXPECT("%08lX", &a, "ABCDEF01");
  a = (Args){0}; p16(&a, 0x1F); EXPECT("%08x", &a, "0000001f");
  a = (Args){0}; p16(&a, 0); EXPECT("%d", &a, "0");
  /* a zero-padded negative number keeps the sign in front (Wine 2562-2566 sign handling) */
  a = (Args){0}; p16(&a, (uint16_t)-5); EXPECT("%05d", &a, "-0005");
  /* '-' wins over '0' */
  a = (Args){0}; p16(&a, 42); EXPECT("%-05d|", &a, "42   |");
  /* a width smaller than the text does not truncate */
  a = (Args){0}; p16(&a, 12345); EXPECT("%2d", &a, "12345");
  /* two arguments of different sizes come off the stack in order */
  a = (Args){0}; p16(&a, 7); p32(&a, 70000UL); p16(&a, 9); EXPECT("%d,%ld,%d", &a, "7,70000,9");
  /* '#' prefix and precision are Wine extensions (UNVERIFIED for Windows 3.1) */
  a = (Args){0}; p16(&a, 255); EXPECT("%#x", &a, "0xff");
  a = (Args){0}; p16(&a, 5); EXPECT("%.3d", &a, "005");
}

static void test_chars_and_strings(void) {
  Args a;
  FARPTR s = fstr("ab");
  a = (Args){0}; p16(&a, 'x'); EXPECT("[%c]", &a, "[x]");
  a = (Args){0}; p16(&a, 'x'); EXPECT("[%3c]", &a, "[  x]");
  a = (Args){0}; p16(&a, 'x'); EXPECT("[%-3c]", &a, "[x  ]");
  a = (Args){0}; p16(&a, 0); EXPECT("[%c]", &a, "[]");               /* NUL is skipped (Wine 2543-2548) */
  a = (Args){0}; p32(&a, s); EXPECT("[%4s]", &a, "[  ab]");
  a = (Args){0}; p32(&a, s); EXPECT("[%-4s]", &a, "[ab  ]");
  a = (Args){0}; p32(&a, s); EXPECT("[%1s]", &a, "[ab]");
  a = (Args){0}; p32(&a, fstr("abcdef")); EXPECT("[%.3s]", &a, "[abc]");    /* precision: Wine extension */
  a = (Args){0}; p32(&a, 0); EXPECT("[%s]", &a, "[]");                     /* null pointer: Wine 2507 */
  a = (Args){0}; p32(&a, fstr("")); EXPECT("[%s]", &a, "[]");
  /* unknown conversion: the conversion character is printed as text */
  a = (Args){0}; EXPECT("a%qb", &a, "aqb");
  /* arguments are consumed in order across mixed conversions */
  a = (Args){0}; p32(&a, s); p16(&a, 3); EXPECT("%s=%d", &a, "ab=3");
}

static void test_limit(void) {
  /* UNVERIFIED limit: 1024 bytes including the NUL -> at most 1023 characters. */
  char big[2001];
  Args a;
  FARPTR s;
  memset(big, 'z', 2000);
  big[2000] = 0;
  s = fstr(big);
  a = (Args){0}; p32(&a, s);
  CHECK_EQ(fmt_run("%s", &a), 1023);
  CHECK_EQ((int)strlen(out()), 1023);
  CHECK_EQ(out()[1023], 0);
  CHECK_EQ(out()[0], 'z');
  /* 1023 characters fit exactly */
  big[1023] = 0;
  s = fstr(big);
  a = (Args){0}; p32(&a, s);
  CHECK_EQ(fmt_run("%s", &a), 1023);
}

/* The output buffer is checked for the whole text written (the old code checked one byte). A
 * segment whose limit ends after 8 bytes must never be written past its end; when the text
 * and its NUL do not fit the call fails with 0 and writes nothing. */
static void test_short_buffer(void) {
  FARPTR blk = far_alloc(64);
  uint16_t sel = sel_alloc(1);
  FARPTR tight;
  uint8_t *raw = (uint8_t *)LIN(blk);
  Args a;
  FARPTR s = fstr("0123456789ABCDEF");
  CHECK(sel != 0);
  if (!sel) return;
  desc_set(sel, desc_base((uint16_t)(blk >> 16)), 7, 0x92); /* 8 bytes: offsets 0..7 */
  tight = MK_FP(sel, 0);
  CHECK(mem_check(tight, 8));
  CHECK(!mem_check(tight, 9));
  memset(raw, 0x5A, 64);
  a = (Args){0}; p32(&a, s);
  {
    FARPTR ap = far_alloc(4);
    memcpy(LIN(ap), a.b, a.n);
    CHECK_EQ((int16_t)_wsprintf(tight, fstr("%s"), ap), 0);
    for (int i = 0; i < 64; i++) CHECK_EQ(raw[i], 0x5A); /* nothing written, nothing past the end */
    /* seven characters and the NUL fill the 8 bytes exactly */
    a = (Args){0}; p32(&a, fstr("1234567"));
    memcpy(LIN(ap), a.b, a.n);
    CHECK_EQ((int16_t)_wsprintf(tight, fstr("%s"), ap), 7);
    CHECK_EQ(memcmp(raw, "1234567", 8), 0);
    CHECK_EQ(raw[8], 0x5A); /* the byte after the segment is untouched */
    /* eight characters need nine bytes */
    memset(raw, 0x5A, 64);
    a = (Args){0}; p32(&a, fstr("12345678"));
    memcpy(LIN(ap), a.b, a.n);
    CHECK_EQ((int16_t)_wsprintf(tight, fstr("%s"), ap), 0);
    for (int i = 0; i < 64; i++) CHECK_EQ(raw[i], 0x5A);
  }
  sel_free(sel, 1);
}

/* ---- the PASCAL/cdecl thunk: USER.420, VARARGS, fixed part 8 bytes ---- */

static void test_thunk(void) {
  uint32_t fp = harness_thunk_far("USER", 420);
  FARPTR o = g_out, f = fstr("%d|%s|%ld");
  FARPTR s = fstr("hi");
  /* cdecl order: the first element is lowest, a 32-bit value is low word first */
  uint16_t args[9] = {(uint16_t)o, (uint16_t)(o >> 16), (uint16_t)f, (uint16_t)(f >> 16),
                      (uint16_t)-7, (uint16_t)s, (uint16_t)(s >> 16), 0x4240, 0x000F};
  uint32_t r;
  CHECK(fp != 0);
  if (!fp) return;
  memset(LIN(g_out), 0, 64);
  r = harness_call_far(fp, HCONV_CDECL, args, 9);
  CHECK_EQ(r & 0xFFFF, 13); /* strlen("-7|hi|1000000") */
  CHECK_EQ(strcmp(out(), "-7|hi|1000000"), 0);
}

int main(void) {
  if (fixture_open() != 0) return 1;
  g_out = far_alloc(1200);
  test_issue_cases();
  test_integers();
  test_chars_and_strings();
  test_limit();
  test_short_buffer();
  test_thunk();
  fixture_close();
  return TEST_DONE();
}
