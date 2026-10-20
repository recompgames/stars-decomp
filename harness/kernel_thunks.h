/* Native-only adapter: registers the plain KERNEL functions of runtime/kernel.h with the
 * harness dispatcher under ("KERNEL", ordinal). Built into libharness.a only. */
#ifndef STARS_HARNESS_KERNEL_THUNKS_H
#define STARS_HARNESS_KERNEL_THUNKS_H

/* Registers every KERNEL part 1 function with harness_register. */
void kernel_register(void);

#endif /* STARS_HARNESS_KERNEL_THUNKS_H */
