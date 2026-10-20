#include "runtime/toolhelp.h"

#include <stdint.h>
#include <string.h>

#include "runtime/mem.h"
#include "runtime/vfs.h"

/* Days from 1980-01-01 to y-m-d (proleptic Gregorian; days_from_civil, H. Hinnant). */
static int64_t days_since_1980(int y, int m, int d) {
  int64_t yy = y - (m <= 2);
  int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
  int64_t yoe = yy - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468 - 3652; /* 1970-01-01 -> 1980-01-01 is 3652 days */
}

uint16_t TimerCount(FARPTR lpTimerInfo) {
  int y, mo, d, h, mi, s;
  uint32_t size, ms;
  uint8_t *p;
  if (!mem_check(lpTimerInfo, TIMERINFO_SIZE)) return 0;
  p = LIN(lpTimerInfo);
  memcpy(&size, p, 4);
  if (size != TIMERINFO_SIZE) return 0;
  rt_clock_get(&y, &mo, &d, &h, &mi, &s);
  ms = (uint32_t)(((days_since_1980(y, mo, d) * 24 + h) * 60 + mi) * 60 + s) * 1000u;
  memcpy(p + 4, &ms, 4);
  memcpy(p + 8, &ms, 4);
  return 1;
}
