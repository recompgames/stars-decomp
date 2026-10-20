/* Minimal C test helpers. No framework.
 * Include from exactly one test .c file, then end main() with: return TEST_DONE();
 */
#ifndef STARS_TESTS_TEST_H
#define STARS_TESTS_TEST_H

#include <stddef.h>
#include <stdio.h>

static int test_checks_run __attribute__((unused)) = 0;
static int test_checks_failed __attribute__((unused)) = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    test_checks_run++;                                                       \
    if (!(cond)) {                                                           \
      test_checks_failed++;                                                  \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                        \
  } while (0)

/* Integer comparison. Both values are printed in hex and decimal on failure. */
#define CHECK_EQ(a, b)                                                       \
  do {                                                                       \
    long long test_a_ = (long long)(a);                                      \
    long long test_b_ = (long long)(b);                                      \
    test_checks_run++;                                                       \
    if (test_a_ != test_b_) {                                                \
      test_checks_failed++;                                                  \
      fprintf(stderr,                                                        \
              "%s:%d: CHECK_EQ(%s, %s) failed: got 0x%llx (%lld), want 0x%llx (%lld)\n", \
              __FILE__, __LINE__, #a, #b,                                    \
              (unsigned long long)test_a_, test_a_,                          \
              (unsigned long long)test_b_, test_b_);                         \
    }                                                                        \
  } while (0)

/* Byte-buffer comparison of n bytes. Reports the first differing offset. */
#define CHECK_MEM(a, b, n)                                                   \
  do {                                                                       \
    const unsigned char *test_pa_ = (const unsigned char *)(a);              \
    const unsigned char *test_pb_ = (const unsigned char *)(b);              \
    size_t test_n_ = (n);                                                    \
    size_t test_i_;                                                          \
    size_t test_bad_ = (size_t)-1;                                           \
    for (test_i_ = 0; test_i_ < test_n_; test_i_++) {                        \
      if (test_pa_[test_i_] != test_pb_[test_i_]) {                          \
        test_bad_ = test_i_;                                                 \
        break;                                                               \
      }                                                                      \
    }                                                                        \
    test_checks_run++;                                                       \
    if (test_bad_ != (size_t)-1) {                                           \
      test_checks_failed++;                                                  \
      fprintf(stderr, "%s:%d: CHECK_MEM(%s, %s, %s) differ at offset %zu\n", \
              __FILE__, __LINE__, #a, #b, #n, test_bad_);                    \
    }                                                                        \
  } while (0)

/* Prints "N checks, M failed". Evaluates to 1 when M > 0, else 0. */
#define TEST_DONE()                                                          \
  (fprintf(stdout, "%d checks, %d failed\n", test_checks_run,                \
           test_checks_failed),                                              \
   test_checks_failed > 0 ? 1 : 0)

#endif /* STARS_TESTS_TEST_H */
