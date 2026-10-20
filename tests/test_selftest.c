/* Checks the test helpers in test.h themselves. */
#include <stdint.h>

#include "test.h"

int main(void) {
  unsigned char a[4] = {1, 2, 3, 4};
  unsigned char b[4] = {1, 2, 3, 4};

  CHECK(1);
  CHECK_EQ(2 + 2, 4);
  CHECK_MEM(a, b, sizeof a);
  return TEST_DONE();
}
