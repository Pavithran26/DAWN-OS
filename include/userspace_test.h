/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USERSPACE_TEST_H
#define DAWN_USERSPACE_TEST_H

#include <stdbool.h>

#ifdef DAWN_KERNEL_TEST
bool userspace_runtime_self_test(void);
bool userspace_spawn_wait_test(void);
bool userspace_exec_test(void);
bool userspace_filesystem_test(void);
#endif

#endif /* DAWN_USERSPACE_TEST_H */
