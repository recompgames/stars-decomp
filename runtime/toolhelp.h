/* TOOLHELP (task stars-0z4.8): the one import the game uses, TimerCount (ordinal 80).
 * Plain C, no harness dependency; the harness adapter is harness/toolhelp_win87em_thunks.c. */
#ifndef STARS_RUNTIME_TOOLHELP_H
#define STARS_RUNTIME_TOOLHELP_H

#include "runtime/include/win16.h"

/* TIMERINFO (Win16): DWORD dwSize (12), DWORD dwmsSinceStart, DWORD dwmsThisVM. */
#define TIMERINFO_SIZE 12u

/* BOOL TimerCount(TIMERINFO FAR *lpti). Returns TRUE (1) and fills both counters when
 * dwSize is 12; FALSE (0) and no write when dwSize is wrong or the 12 bytes are not
 * addressable. Both counters are the runtime clock (rt_clock_get, runtime/vfs.h) as
 * milliseconds since 1980-01-01 00:00:00, modulo 2^32 like GetTickCount. In standard mode
 * and with no VM switch, Windows reports the same value in both (Wine does the same). */
uint16_t TimerCount(FARPTR lpTimerInfo);

#endif /* STARS_RUNTIME_TOOLHELP_H */
