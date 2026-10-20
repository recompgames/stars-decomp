/* Native-only adapter: registers the plain USER part 1 functions of runtime/user.h (windows,
 * messages, painting) with the harness dispatcher under ("USER", ordinal). Built into
 * libharness.a only. */
#ifndef STARS_HARNESS_USER_THUNKS_H
#define STARS_HARNESS_USER_THUNKS_H

/* Registers every USER part 1 function with harness_register. Call after user_init(). */
void user_register(void);

#endif /* STARS_HARNESS_USER_THUNKS_H */
