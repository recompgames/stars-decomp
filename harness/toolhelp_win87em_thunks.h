/* Native-only adapter: registers TOOLHELP.TimerCount (80) and WIN87EM.__fpMath (1) of
 * runtime/toolhelp.h and runtime/win87em.h with the harness dispatcher. */
#ifndef STARS_HARNESS_TOOLHELP_WIN87EM_THUNKS_H
#define STARS_HARNESS_TOOLHELP_WIN87EM_THUNKS_H

void toolhelp_win87em_register(void);

#endif /* STARS_HARNESS_TOOLHELP_WIN87EM_THUNKS_H */
