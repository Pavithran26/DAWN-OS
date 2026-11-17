/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_USER_MODE_H
#define DAWN_USER_MODE_H
#include <stdbool.h>
#include <stdint.h>
/* Validate an entry point and initial stack in the active page-table root.
 * This does not create an isolated address space or a process. */
bool user_mode_validate_entry(uint64_t entry,uint64_t stack_top);
/* Enter CPL3 and do not return. Caller must arrange a safe kernel continuation
 * before using this primitive; this low-level transition is not a process API. */
_Noreturn void user_mode_enter(uint64_t entry,uint64_t stack_top);
#ifdef DAWN_KERNEL_TEST
bool user_mode_self_test(void);
#endif
#endif
